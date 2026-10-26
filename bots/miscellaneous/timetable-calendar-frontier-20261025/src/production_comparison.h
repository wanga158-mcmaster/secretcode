#pragma once
// Retained exact comparison of caller-owned immutable byte vectors. The key must
// change whenever either source's contents/identity changes; equal size alone
// cannot detect mutation of an already compared prefix. No round/epoch is used.
// Only sizes, a generation key and scalar counters are stored: no pointers/heap.
#include <cstddef>
#include <cstdint>
#include <span>

namespace tt::production_comparison {
struct Key
{
    std::uint64_t query{},field{};
    friend bool operator==(Key,Key)=default;
};
enum class Status { NeedCompute, Equal, Different, Unknown };

// A retained nomination compares physical provenance to physical provenance;
// derived outgoing damage can differ from raw strength at the SAME state.
template<class Packet,class Point>
bool same_raw_body(const Packet& packet,Point point,float hp,float maximum,float raw_strength,
                   int reach,std::uint32_t gems,std::uint32_t gold)
{
    return packet.pos==point && packet.hp==hp && packet.max_hp==maximum &&
        packet.raw_strength==raw_strength && packet.attack_range==reach &&
        packet.gems==gems && packet.gold==gold;
}

class Comparison
{
public:
    Comparison(std::size_t lhs_size,std::size_t rhs_size,Key key) noexcept
        : lhs_size_{lhs_size},rhs_size_{rhs_size},key_{key},
          status_{lhs_size!=rhs_size?Status::Different:lhs_size==0?Status::Equal:Status::NeedCompute} {}

    Status status() const noexcept { return status_; }
    std::size_t compared() const noexcept { return compared_; }
    std::size_t last_work() const noexcept { return last_work_; }

    // Every observed byte pair consumes one unit, including a differing pair.
    // Even completed comparisons validate exact registered sizes and key first.
    // Unknown is terminal; a new query needs a newly constructed Comparison.
    Status advance(std::span<const std::uint8_t> lhs,std::span<const std::uint8_t> rhs,
                   Key actual,std::size_t byte_units) noexcept
    {
        last_work_=0;
        if (status_==Status::Unknown) return status_;
        if (actual!=key_ || lhs.size()!=lhs_size_ || rhs.size()!=rhs_size_)
        { status_=Status::Unknown; return status_; }
        if (status_!=Status::NeedCompute) return status_;
        while (last_work_<byte_units && compared_<lhs_size_)
        {
            const bool same=lhs[compared_]==rhs[compared_];
            ++compared_;
            ++last_work_;
            if (!same) { status_=Status::Different; return status_; }
        }
        if (compared_==lhs_size_) status_=Status::Equal;
        return status_;
    }

private:
    std::size_t lhs_size_{},rhs_size_{};
    Key key_;
    std::size_t compared_{},last_work_{};
    Status status_;
};
} // namespace tt::production_comparison
