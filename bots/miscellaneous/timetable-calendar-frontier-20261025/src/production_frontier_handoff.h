#pragma once
// A funded publication receipt is one optional pre-action opportunity, not a
// forecast, observer, planner or command authority. No SDK calls occur here.
#include <cstdint>
#include <optional>

namespace tt::production_frontier_handoff {
struct Point {
    int x{},y{};
    friend bool operator==(const Point&,const Point&)=default;
};
struct Key {
    int funded_epoch{-1};
    std::uint64_t query{},body{},field{};
    Point origin;
    int phase{},group{-1}; // Caller-owned incumbent enum/index, never native IDs.
    friend bool operator==(const Key&,const Key&)=default;
};
enum class Readiness { None,ProgrammeOnly,Valued };
struct Frame {
    int epoch{-1};
    std::uint64_t query{},body{},field{};
    Point origin;
    int phase{},group{-1};
    // fresh_input means actual current input/epoch fence, not merely a larger
    // round number. Body revision is a dependency ID, not fresh self proof.
    bool fresh_input{},route_ready{},quiet{},no_enemy{},before_command{};
    // Valued requires the caller's complete programme AND checked exact index
    // or qualified dense prefix. ProducerReady alone is ProgrammeOnly.
    Readiness readiness{Readiness::None};
};
enum class Opportunity { Closed,Deferred,Fallback,Validate };
enum class State { Empty,Pending,Checking,Closed };

class Receipt {
public:
    bool arm(const Key& key) {
        close();
        if(key.funded_epoch<0||key.group<0||key.origin.x<0||key.origin.y<0)return false;
        key_=key;state_=State::Pending;last_checked_epoch_=-1;return true;
    }
    Opportunity opportunity(const Frame& frame) {
        if(state_!=State::Pending||!key_)return Opportunity::Closed;
        if(!frame.fresh_input)return Opportunity::Deferred;
        // Cheap actual pose/incumbent checks precede readiness and duplicate
        // checks, so movement closes even after a no-policy check this epoch.
        if(frame.query!=key_->query||frame.body!=key_->body||frame.field!=key_->field||
           frame.origin!=key_->origin||frame.phase!=key_->phase||frame.group!=key_->group) {
            close();return Opportunity::Closed;
        }
        if(frame.epoch<=key_->funded_epoch||frame.epoch<=last_checked_epoch_)
            return Opportunity::Deferred;
        if(!frame.route_ready||!frame.quiet||!frame.no_enemy||!frame.before_command)
            return Opportunity::Deferred;
        last_checked_epoch_=frame.epoch;
        if(frame.readiness!=Readiness::Valued)return Opportunity::Fallback;
        // The expensive validation attempt is consumed before leaving this
        // pure gate. No subsequent eager redispatch may repeat it, even if the
        // collector fails or the adapter forgets to close explicitly.
        state_=State::Checking;return Opportunity::Validate;
    }
    // Accepted selection or failed actual collector/Life validation both retire
    // this receipt. Actual full self/Life admission remains entirely caller-owned.
    void consume() {close();}
    void close() {key_.reset();state_=State::Closed;last_checked_epoch_=-1;}
    bool pending() const {return state_==State::Pending;}
    bool checking() const {return state_==State::Checking;}
    State state() const {return state_;}
    const std::optional<Key>& key() const {return key_;}
    int last_checked_epoch() const {return last_checked_epoch_;}
private:
    std::optional<Key> key_;
    State state_{State::Empty};
    int last_checked_epoch_{-1};
};
} // namespace tt::production_frontier_handoff
