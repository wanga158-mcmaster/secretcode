#pragma once
#include "route.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace mosaic::adaptive {

struct EscapeKey {
    std::uint64_t route_revision{},route_semantic{},source_revision{};
    friend constexpr bool operator==(EscapeKey,EscapeKey)=default;
};
struct EscapePotential {std::uint64_t amount{};bool complete{};};
enum class EscapeStatus : std::uint8_t {idle,preparing,quiet,path,unsupported,invalid};
struct EscapeProgress {
    EscapeStatus status{EscapeStatus::idle};unsigned units{},source_samples{},steps_added{};bool progressed{};
};
struct EscapePrice {
    bool complete{},survivable{},saturated{},shield_used{};
    std::uint64_t hp_loss{},hp_after{},raw_damage{};unsigned phases{};
};
struct EscapeMoveUpdate {bool recorded{},trimmed{},reset{};};

// One bounded NPC-domain exit candidate, not command permission, future
// occupancy/success, a service owner, player-response safety or utility.
// The source is immutable, O(1), and complete over ALL possible NPC sources;
// unknown life/aggro must retain its conservative source bound. Dependencies
// change before editing its source or Route's borrowed passability board.
class Escape {
public:
    static constexpr unsigned max_steps=64,max_origin_phases=64;
    constexpr Escape()noexcept=default;
    Escape(const Escape&)=delete;Escape& operator=(const Escape&)=delete;
    Escape(Escape&&)=delete;Escape& operator=(Escape&&)=delete;

    bool begin(Epoch actual,Point origin,EscapeKey dependencies)noexcept {
        if(actual.round<0||origin.x<0||origin.y<0||!dependencies.source_revision||
           ((dependencies.route_revision==0)!=(dependencies.route_semantic==0))||
           (status_!=EscapeStatus::idle&&before(actual,epoch_)))return false;
        if(status_!=EscapeStatus::idle&&origin==origin_&&dependencies==key_){
            epoch_=actual;++mutation_;return true;
        }
        ++mutation_;epoch_=actual;origin_=origin;key_=dependencies;head_=tail_=0;
        points_[0]=origin;status_=EscapeStatus::preparing;stage_=Stage::origin;
        candidate_count_=candidate_cursor_=0;return true;
    }
    void retire()noexcept {++mutation_;status_=EscapeStatus::idle;key_={};head_=tail_=0;}

    // Every source sample and each <=8-neighbour nomination is a separately
    // admitted bounded leaf. Denial retains the exact candidate/sample cursor.
    // Per-origin Ready works while the global field is still Preparing.
    template<std::size_t Cells,class Source,class Admit>
    EscapeProgress advance(const Route<Cells>& route,Source&& source,Admit&& admit,unsigned leaf_cap=8){
        EscapeProgress out{status_};const auto stamp=mutation_;
        auto matches=[&]{return stamp==mutation_&&route.revision()==key_.route_revision&&route.semantic()==key_.route_semantic;};
        if(status_!=EscapeStatus::preparing)return out;
        if(!matches()){status_=EscapeStatus::invalid;out.status=status_;return out;}
        while(out.units<leaf_cap&&status_==EscapeStatus::preparing){
            if(!bool(admit())||!matches())break;
            ++out.units;
            if(stage_==Stage::origin){
                const auto potential=source(origin_);++out.source_samples;
                if(!matches())break;
                if(!potential.complete){status_=EscapeStatus::invalid;break;}
                potentials_[0]=potential.amount;out.progressed=true;
                if(!potential.amount){status_=EscapeStatus::quiet;break;}
                stage_=Stage::candidates;continue;
            }
            const auto& field=route.navigation();
            if(stage_==Stage::candidates){
                const auto at=field.distance(points_[tail_]);
                if(!at){
                    if(field.status()!=RouteStatus::preparing)status_=EscapeStatus::unsupported;
                    break;
                }
                if(tail_==max_steps){status_=EscapeStatus::unsupported;break;}
                candidate_count_=field.nominees(points_[tail_],{},candidates_);
                if(!candidate_count_){status_=EscapeStatus::unsupported;break;}
                before_distance_=*at;candidate_cursor_=0;best_amount_=0;best_index_=0;
                stage_=Stage::sample;out.progressed=true;continue;
            }
            const auto candidate=candidates_[candidate_cursor_];
            const auto distance=field.distance(candidate);
            if(!distance||*distance>=before_distance_){status_=EscapeStatus::invalid;break;}
            const auto potential=source(candidate);++out.source_samples;
            if(!matches())break;
            if(!potential.complete){status_=EscapeStatus::invalid;break;}
            if(candidate_cursor_==0||potential.amount<best_amount_){best_index_=candidate_cursor_;best_amount_=potential.amount;}
            ++candidate_cursor_;out.progressed=true;
            if(candidate_cursor_==candidate_count_){
                points_[++tail_]=candidates_[best_index_];potentials_[tail_]=best_amount_;++out.steps_added;
                stage_=Stage::candidates;
                if(!best_amount_)status_=EscapeStatus::path;
            }
        }
        out.status=status_;return out;
    }

    // Only an accepted Move plus actual matching pose readback trims a prefix.
    // A raw FALSE/nonattempt leaves the candidate intact; it grants no retry.
    // Accepted off-path motion retires it. Serial duplicates cannot trim twice.
    EscapeMoveUpdate observe_move(const MoveRecord& receipt,Epoch actual_after,Point actual_pose)noexcept {
        EscapeMoveUpdate out{};
        if(!receipt.serial||receipt.serial<last_move_.serial||receipt.epoch.round<0||
           unsigned(receipt.outcome)>unsigned(MoveOutcome::unknown))return out;
        if(receipt.serial==last_move_.serial&&
           (last_move_.outcome!=MoveOutcome::unknown||receipt.epoch!=last_move_.epoch||
            receipt.from!=last_move_.from||receipt.to!=last_move_.to||
            (receipt.outcome!=MoveOutcome::applied&&receipt.outcome!=MoveOutcome::refused)))return out;
        last_move_=receipt;out.recorded=true;
        if(receipt.outcome!=MoveOutcome::applied)return out;
        if(status_==EscapeStatus::idle)return out;
        if(receipt.epoch!=epoch_||before(actual_after,receipt.epoch)||receipt.from!=origin_||
           actual_pose!=receipt.to||head_==tail_||points_[head_+1]!=receipt.to){
            retire();out.reset=true;return out;
        }
        ++head_;origin_=actual_pose;epoch_=actual_after;++mutation_;out.trimmed=true;
        if(complete()&&head_==tail_)status_=EscapeStatus::quiet;
        return out;
    }

    // Sequential whole-tail NPC arithmetic. origin_phases=0 describes an
    // immediate melee Move; 1 covers ranged M already spent OR a Work batch
    // that holds one origin phase before exiting. Additional actual waiting
    // must be passed explicitly. This function never chooses that phase class.
    // One refusal inserts one extra camp phase at ANY before-move pose.
    // Current owner END decrements shield timers before the next camp batch.
    // Timers0 and1 both cover that first future batch, then expire at the next
    // owner START. Timer2 covers two future batches. Unknown timing uses HP.
    EscapePrice price(const game::SelfFact& self,EscapeKey dependencies,
                      unsigned origin_phases=0,bool one_refusal=true)const noexcept {
        if(!complete()||dependencies!=key_||self.epoch!=epoch_||self.pose!=origin_||
           !valid_resources(self)||origin_phases>max_origin_phases)return {};
        return price_sequence(self,origin_phases,one_refusal,potentials_[head_],false);
    }

    // Conditional ingress: actual.pose -> stored origin -> complete exit.
    // The first landing's camp batch uses CURRENT actual HP/shield and is the
    // first future phase. No future pose/body fact is fabricated. This prices
    // the NPC domain only; fresh SDK legality/attempt and readback still decide
    // whether movement happens, and no future destination is asserted empty.
    EscapePrice price_move(const game::SelfFact& actual,EscapeKey dependencies,
                           EscapePotential actual_origin_potential,bool one_refusal=true)const noexcept {
        if(!complete()||dependencies!=key_||actual.epoch!=epoch_||!valid_resources(actual)||
           !actual.can_move(actual.epoch)||!actual_origin_potential.complete||
           actual.pose.x<0||actual.pose.y<0||!adjacent(actual.pose,origin_))return {};
        return price_sequence(actual,0,one_refusal,actual_origin_potential.amount,true);
    }
    [[nodiscard]] EscapeStatus status()const noexcept{return status_;}
    [[nodiscard]] bool complete()const noexcept{return status_==EscapeStatus::quiet||status_==EscapeStatus::path;}
    [[nodiscard]] Epoch epoch()const noexcept{return epoch_;}
    [[nodiscard]] Point origin()const noexcept{return origin_;}
    [[nodiscard]] EscapeKey key()const noexcept{return key_;}
    [[nodiscard]] unsigned steps()const noexcept{return tail_-head_;}
    [[nodiscard]] std::span<const Point> path()const noexcept{return {points_.data()+head_+1,tail_-head_};}
    [[nodiscard]] std::optional<Point> next()const noexcept{return head_<tail_?std::optional<Point>{points_[head_+1]}:std::nullopt;}

private:
    enum class Stage : std::uint8_t {origin,candidates,sample};
    struct Damage {std::uint64_t hp_loss{},shield{};unsigned phases{};bool valid{};};
    std::array<Point,max_steps+1> points_{};std::array<std::uint64_t,max_steps+1> potentials_{};
    std::array<Point,8> candidates_{};EscapeKey key_{};Epoch epoch_{};Point origin_{};
    std::uint64_t mutation_{},best_amount_{};MoveRecord last_move_{};
    unsigned head_{},tail_{},candidate_count_{},candidate_cursor_{},best_index_{};int before_distance_{};
    EscapeStatus status_{EscapeStatus::idle};Stage stage_{Stage::origin};
    static constexpr bool before(Epoch a,Epoch b)noexcept {
        return a.round!=b.round?a.round<b.round:a.revision!=b.revision?a.revision<b.revision:a.attempt<b.attempt;
    }
    static constexpr bool adjacent(Point a,Point b)noexcept {
        const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
        return std::max(dx<0?-dx:dx,dy<0?-dy:dy)==1;
    }
    static bool valid_resources(const game::SelfFact& self)noexcept {
        return self.body_actual&&self.alive&&std::isfinite(self.hp)&&self.hp>0;
    }
    EscapePrice price_sequence(const game::SelfFact& actual,unsigned origin_phases,bool one_refusal,
                               std::uint64_t starting_potential,bool ingress)const noexcept {
        EscapePrice out{};const auto hp=resource(actual.hp);
        std::uint64_t shield=0,shield_phases=0;
        if(actual.buffs_actual&&std::isfinite(actual.shield)&&actual.shield>0&&actual.shield_time>=0){
            shield=resource(actual.shield);shield_phases=std::uint64_t(std::max(1,actual.shield_time));out.shield_used=shield!=0;
        }
        Damage plain{0,shield,0,true},refused{};
        bool saturated=false;std::uint64_t raw=0,max_before=0;
        for(unsigned n=0;n<origin_phases;++n){
            apply(plain,starting_potential,shield_phases,saturated);raw=add(raw,starting_potential,saturated);
        }
        const unsigned moves=(tail_-head_)+unsigned(ingress);
        for(unsigned step=0;step<moves;++step){
            const auto before_amount=step==0?starting_potential:potentials_[head_+step-unsigned(ingress)];
            const auto after_amount=potentials_[head_+step+1-unsigned(ingress)];
            max_before=std::max(max_before,before_amount);
            Damage inserted=plain;
            if(one_refusal){apply(inserted,before_amount,shield_phases,saturated);apply(inserted,after_amount,shield_phases,saturated);}
            if(refused.valid)apply(refused,after_amount,shield_phases,saturated);
            if(one_refusal&&worse(inserted,refused))refused=inserted;
            apply(plain,after_amount,shield_phases,saturated);raw=add(raw,after_amount,saturated);
        }
        if(one_refusal&&moves)raw=add(raw,max_before,saturated);
        const auto worst=refused.valid&&worse(refused,plain)?refused:plain;
        out.saturated=saturated;out.complete=!saturated;out.hp_loss=worst.hp_loss;
        out.hp_after=hp>worst.hp_loss?hp-worst.hp_loss:0;
        out.survivable=out.complete&&hp>worst.hp_loss;out.raw_damage=raw;
        out.phases=origin_phases+moves+unsigned(one_refusal&&moves);return out;
    }
    static std::uint64_t resource(float amount)noexcept {
        if(amount>=float(std::numeric_limits<std::uint64_t>::max()))return std::numeric_limits<std::uint64_t>::max();
        return std::uint64_t(std::floor(amount));
    }
    static std::uint64_t add(std::uint64_t a,std::uint64_t b,bool& saturated)noexcept {
        if(b>std::numeric_limits<std::uint64_t>::max()-a){saturated=true;return std::numeric_limits<std::uint64_t>::max();}return a+b;
    }
    static void apply(Damage& state,std::uint64_t amount,std::uint64_t shield_phases,bool& saturated)noexcept {
        if(state.phases>=shield_phases)state.shield=0;
        const auto absorbed=std::min(state.shield,amount);state.shield-=absorbed;
        state.hp_loss=add(state.hp_loss,amount-absorbed,saturated);++state.phases;
    }
    static bool worse(const Damage& a,const Damage& b)noexcept {
        return a.valid&&(!b.valid||a.hp_loss>b.hp_loss||(a.hp_loss==b.hp_loss&&a.shield<b.shield));
    }
};

} // namespace mosaic::adaptive
