#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace plan4 {
// Storage only: every lease clears/refills from a complete new export. The
// cleanup list is never evidence about current occupancy outside that lease.
template<class Position> class FreshDynamicGrid {
public:
    struct Key {
        int width=0,height=0,actor=-1,indexing=1;
        std::uint64_t map_epoch=0;
        bool operator==(const Key&) const = default;
    };
    enum class Stage { old_clear, old_cleared, exported, owned, new_mark, complete };
    struct NoCheckpoint { void operator()(Stage,std::size_t) const noexcept {} };
private:
    std::vector<unsigned char> cells_;
    std::vector<Position> previous_points_;
    Key key_{};
    bool bound_=false,leased_=false,view_valid_=false;
    static bool supported(const Key& key) {
        return key.width>0 && key.height>0 && key.actor>=0 && key.map_epoch && key.indexing==1 &&
            std::uint64_t(key.width)*std::uint64_t(key.height)<=std::uint64_t(std::numeric_limits<int>::max());
    }
    std::size_t index(Position p) const { return std::size_t(p.y)*std::size_t(key_.width)+std::size_t(p.x); }
public:
    class Lease {
        FreshDynamicGrid* owner_=nullptr;
        bool complete_=false;
        explicit Lease(FreshDynamicGrid& owner):owner_(&owner) { owner.leased_=true;owner.view_valid_=false; }
        friend class FreshDynamicGrid;
        void bind(Key key) {
            if(owner_->bound_ && owner_->key_==key) return;
            // Allocation failure leaves the old binding/tracking intact. No
            // old coordinate is interpreted with a new linear indexing width.
            std::vector<unsigned char> replacement(std::size_t(key.width)*std::size_t(key.height),0);
            owner_->cells_.swap(replacement);
            owner_->previous_points_.clear();
            owner_->key_=key;owner_->bound_=true;
        }
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)),complete_(other.complete_) {}
        ~Lease() { if(owner_) { owner_->view_valid_=false;owner_->leased_=false; } }
        explicit operator bool() const { return owner_!=nullptr; }
        template<class Export,class Checkpoint=NoCheckpoint>
        void refill(Export&& fresh,Checkpoint checkpoint={}) {
            if(!owner_) throw std::logic_error("fresh grid has no exclusive lease");
            complete_=false;owner_->view_valid_=false;
            for(std::size_t n=0;n<owner_->previous_points_.size();++n) {
                checkpoint(Stage::old_clear,n);
                owner_->cells_[owner_->index(owner_->previous_points_[n])]=0;
            }
            checkpoint(Stage::old_cleared,owner_->previous_points_.size());
            auto next=std::forward<Export>(fresh)();
            checkpoint(Stage::exported,next.size());
            static_assert(std::is_nothrow_move_assignable_v<std::vector<Position>>);
            owner_->previous_points_=std::move(next);
            checkpoint(Stage::owned,owner_->previous_points_.size());
            for(std::size_t n=0;n<owner_->previous_points_.size();++n) {
                owner_->cells_[owner_->index(owner_->previous_points_[n])]=1;
                checkpoint(Stage::new_mark,n);
            }
            checkpoint(Stage::complete,owner_->previous_points_.size());
            complete_=true;
        }
        void publish() {
            if(!owner_ || !complete_) throw std::logic_error("fresh grid publication before complete refill");
            owner_->view_valid_=true;
        }
        const std::vector<unsigned char>& view() const {
            if(!owner_ || !owner_->view_valid_) throw std::logic_error("fresh grid view outside prepared invocation");
            return owner_->cells_;
        }
    };
    Lease acquire(Key key) {
        // A nested call uses its original dense local path; it cannot mutate
        // the outer invocation's storage or prepared callback view.
        if(leased_ || !supported(key)) return {};
        Lease result(*this);result.bind(key);return result;
    }
};
} // namespace plan4
