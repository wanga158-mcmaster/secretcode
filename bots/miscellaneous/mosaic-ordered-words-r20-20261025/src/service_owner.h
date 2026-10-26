#pragma once
// Retained ownership of an ENABLING observation, not work at a nominal perch.
// The adapter supplies actual current owner inputs, actual fresh life reads and
// monotonically numbered receipts for relevant physical/information progress.
// Field preparation, a new epoch, a planning nonce or a repeated nomination is
// not such a receipt. This component contains no SDK, game or fuel constants.
#include <cstdint>
#include <cstddef>
#include <vector>

namespace mosaic::service_owner {
using Frame=std::uint64_t;
using Receipt=std::uint64_t;
enum class Life {unknown,alive,dead};
enum class Status {idle,active,completed,blocked,invalid_limit};

class Observation {
public:
    // The caller chooses the finite count of genuinely nonprogressing owner
    // inputs. A zero limit is invalid; it never starts an active owner.
    explicit Observation(unsigned blocked_limit):limit_(blocked_limit){}

    bool active()const{return status()==Status::active;}
    bool completed()const{return status()==Status::completed;}
    int job()const{return job_;}
    int member()const{return member_;}
    Status status()const{return !limit_?Status::invalid_limit:current_entry()?current_entry()->status:Status::idle;}
    Life life()const{return current_entry()?current_entry()->life:Life::unknown;}
    unsigned blocked_inputs()const{return current_entry()?current_entry()->blocked:0;}
    unsigned blocked_limit()const{return limit_;}
    Frame frame()const{return frame_;}
    Frame observed_frame()const{return current_entry()?current_entry()->observed_frame:0;}
    Receipt last_receipt()const{return last_receipt_;}
    std::size_t catalog_size()const{return entries_.size();}

    // Entering the next ACTUAL owner input only opens one new accounting slot;
    // it never completes the observation or renews its nonprogress allowance.
    bool enter(Frame frame){
        if(seen_frame_&&frame<frame_)return false;
        seen_frame_=true;frame_=frame;
        if(auto* entry=current_entry())touch(*entry,frame);
        return true;
    }

    // True means the semantic job/member key changed, not physical progress.
    // Repeating OR RESELECTING a completed/blocked key cannot reopen it. The
    // finite caller catalog retains each key's PARTIAL nonprogress budget too:
    // A->B->A is selection, not a fresh premise or a new lease. A new job/member
    // selects its own entry; negative keys deselect without forgetting history.
    bool request(int job,int member,Frame frame){
        if(!enter(frame)||!limit_)return false;
        if(have_key_&&job==job_&&member==member_)return false;
        have_key_=true;job_=job;member_=member;selected_=none;
        if(job>=0&&member>=0){
            selected_=find(job,member);
            if(selected_==none){
                entries_.push_back(Entry{});selected_=entries_.size()-1;
                entries_.back().job=job;entries_.back().member=member;
            }
            touch(entries_[selected_],frame);
        }
        return true;
    }

    // This is the ONLY completion operation. Call it only after the adapter has
    // actually queried this member's life in this CURRENT ready owner input.
    // A real read can resolve ANY matching previously nominated catalog entry,
    // including a blocked key while another owner is selected. It never changes
    // selection or reactivates the resolved entry. This permits actual reads
    // before selection without laundering a closed key through another member.
    // Unknown/unnominated keys and stale/future frames are no completion.
    bool observed(int job,int member,Frame frame,Life actual){
        if(!current(frame)||!limit_||(actual!=Life::alive&&actual!=Life::dead))return false;
        const auto at=find(job,member);if(at==none)return false;
        auto& entry=entries_[at];if(entry.status==Status::completed)return false;
        touch(entry,frame);entry.status=Status::completed;entry.life=actual;
        entry.observed_frame=frame;entry.blocked=0;entry.progressed=true;
        return true;
    }

    // An explicit new observation episode may reuse this semantic member ONLY
    // after an actual life completion. The adapter calls this when that fact
    // needs reacquisition (e.g. after losing current visibility or a real world
    // effect), not merely because the next epoch/field request happened.
    // A blocked closure has no completion receipt and cannot use this reset.
    bool renew_after_observed(Frame frame){
        if(!enter(frame)||!completed())return false;
        auto& entry=*current_entry();entry.status=Status::active;entry.life=Life::unknown;
        entry.blocked=0;entry.progressed=entry.accounted=false;return true;
    }

    // A receipt certifies relevant ACTUAL progress toward enabling this read
    // (e.g. an accepted advancing step or new useful personal visibility).
    // Number receipts monotonically across this object, including key changes.
    // Command nomination/field commit alone is not an admissible caller receipt.
    // Progress resets the blocked streak but NEVER completes or reopens an owner.
    bool progress(Frame frame,Receipt actual_receipt){
        if(!active()||!current(frame)||!actual_receipt||actual_receipt<=last_receipt_)return false;
        auto& entry=*current_entry();last_receipt_=actual_receipt;
        entry.blocked=0;entry.progressed=true;return true;
    }

    // True only on the transition that closes this owner. Count at most one
    // genuinely nonprogressing input; repeated calls, epochs with no accounting
    // and inputs with an actual progress receipt never manufacture more time.
    bool blocked(Frame frame){
        if(!active()||!current(frame))return false;
        auto& entry=*current_entry();
        if(entry.progressed||entry.accounted)return false;
        entry.accounted=true;++entry.blocked;
        if(entry.blocked<limit_)return false;
        entry.status=Status::blocked;return true;
    }

private:
    struct Entry {
        int job{-1},member{-1};
        unsigned blocked{};
        Frame frame{},observed_frame{};
        Status status{Status::active};
        Life life{Life::unknown};
        bool seen_frame{},progressed{},accounted{};
    };
    static constexpr std::size_t none=std::size_t(-1);
    // The adapter nominates a finite actual member catalog. Entries are sparse
    // and created once per semantic key; switching never deletes their budgets.
    // No SDK catalog size or game/fuel constant is embedded here.
    std::vector<Entry> entries_;
    std::size_t selected_{none};
    unsigned limit_{};
    int job_{-1},member_{-1};
    Frame frame_{};
    Receipt last_receipt_{};
    bool have_key_{},seen_frame_{};
    bool current(Frame frame)const{return seen_frame_&&frame==frame_;}
    Entry* current_entry(){return selected_<entries_.size()?&entries_[selected_]:nullptr;}
    const Entry* current_entry()const{return selected_<entries_.size()?&entries_[selected_]:nullptr;}
    std::size_t find(int job,int member)const{
        if(const auto* entry=current_entry();entry&&entry->job==job&&entry->member==member)return selected_;
        for(std::size_t i=0;i<entries_.size();++i)
            if(entries_[i].job==job&&entries_[i].member==member)return i;
        return none;
    }
    static void touch(Entry& entry,Frame frame){
        if(!entry.seen_frame||frame!=entry.frame){entry.progressed=entry.accounted=false;}
        entry.seen_frame=true;entry.frame=frame;
    }
};
}
