#pragma once
#include "wire.h"
#include "../assignment-common/draft.h"
#include <wasi/api.h>

namespace plan3_correlator {
enum class DraftStatus {pending,published,unavailable};
inline Seal read_seal(bool control,unsigned teamSize) {
    const cambc::draft_memory_buffer current; // Fresh authoritative read, not startup snapshot.
    auto seal=decode(std::span<const std::byte>{current.data(),current.size()},
        unsigned(cambc::map::width()),unsigned(cambc::map::height()),control);
    if(seal.policy.state==State::known && seal.policy.actors!=plan3_runtime::market_fixed_roster(teamSize))return {};
    return seal;
}
class Draft {
    plan3_assignment::DraftAllocator public_{plan3_assignment::Policy::matchmaker_exact};
    bool control_=false,entropyAttempted_=false,unavailable_=false,published_=false;
    std::uint32_t ticket_=0;
    Seal sealed_;
public:
    explicit Draft(bool control):control_(control) {}
    DraftStatus step() {
        if(published_)return DraftStatus::published;
        if(unavailable_ || !cambc::is_draft())return DraftStatus::unavailable;
        try {
            if(!entropyAttempted_) {
                if(cambc::fuel_remaining()<65000)return DraftStatus::pending;
                entropyAttempted_=true; // Failure is permanent, never resampled.
                if(__wasi_random_get(reinterpret_cast<std::uint8_t*>(&ticket_),sizeof(ticket_))!=0) {
                    unavailable_=true;return DraftStatus::unavailable;
                }
            }
            const auto status=public_.step();
            if(status==plan3_assignment::DraftStatus::unavailable) {unavailable_=true;return DraftStatus::unavailable;}
            if(status!=plan3_assignment::DraftStatus::published || cambc::fuel_remaining()<65000)return DraftStatus::pending;
            // Reuse the public complete matrix and mapper as-is. Its exact
            // matching result is NOT substituted for the joint bundle policy.
            auto policy=build(public_.matrix());
            if(policy.state!=State::known) {unavailable_=true;return DraftStatus::unavailable;}
            sealed_.policy=std::move(policy);sealed_.control=control_;sealed_.ticket=ticket_;
            sealed_.width=unsigned(cambc::map::width());sealed_.height=unsigned(cambc::map::height());
            sealed_.matrixFingerprint=public_.matrix_fingerprint();
            const auto bytes=encode(sealed_);
            if(!bytes)throw std::runtime_error("Correlator invalid complete draft seal");
            cambc::draft_memory=*bytes;
            const cambc::draft_memory_buffer joined;
            if(joined.size()!=bytes->size() || !std::equal(joined.begin(),joined.end(),bytes->begin()))
                throw std::runtime_error("Correlator draft seal readback failed");
            published_=true;return DraftStatus::published;
        } catch(...) {unavailable_=true;throw;}
    }
};
} // namespace plan3_correlator
