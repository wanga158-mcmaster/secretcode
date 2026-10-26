#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace h12_witnessed_handoff {
enum class Phase : std::uint8_t { idle,offer,accept,released,paid,cancel };
struct Packet {
    std::uint16_t tile=0,offer=0,deadline=0;
    std::uint8_t clearer=0,holder=0,report=0;
    Phase phase=Phase::idle;
};
// Explicit shifts are the wire format; no C++ layout enters shared memory.
inline std::uint64_t encode(const Packet&p) {
    return std::uint64_t(p.tile)|(std::uint64_t(p.clearer&15)<<16)|
        (std::uint64_t(p.holder&15)<<20)|(std::uint64_t(static_cast<unsigned>(p.phase)&7)<<24)|
        (std::uint64_t(p.report&31)<<27)|(std::uint64_t(p.offer)<<32)|(std::uint64_t(p.deadline)<<48);
}
inline Packet decode(std::uint64_t word) {
    Packet p;p.tile=word&65535;p.clearer=(word>>16)&15;p.holder=(word>>20)&15;
    p.phase=static_cast<Phase>((word>>24)&7);p.report=(word>>27)&31;
    p.offer=(word>>32)&65535;p.deadline=(word>>48)&65535;return p;
}
inline bool same_key(const Packet&a,const Packet&b) {
    return a.tile==b.tile&&a.clearer==b.clearer&&a.holder==b.holder&&a.offer==b.offer;
}
inline bool source_observation_needed(const Packet&p,int round,int type) {
    // A prospective offer has its canonical source and roles before the
    // adapter supplies the current round used to assign its dates.
    return (type==p.clearer||type==p.holder)&&(p.deadline==0||round<=p.deadline);
}
inline bool physical_type(int type) {return type>=0&&type<=12&&type!=3&&type!=4;}
inline bool valid(const Packet&p,int round) {
    return p.tile!=0&&p.clearer==10&&physical_type(p.holder)&&p.holder!=p.clearer&&
        p.phase>=Phase::offer&&p.phase<=Phase::cancel&&round>=0&&round<=65535&&
        p.deadline>p.offer&&p.deadline-p.offer<32&&p.report<=p.deadline-p.offer&&
        round>=p.offer&&p.offer+p.report<=round;
}
struct Observation {
    int round=0,type=-1,capture=5,bag=0;
    float hp=0,maximum=1;
    bool alive=true,channel=false,source_valid=false,reward=false,renewed=false,on_source=false;
    bool holder_visible_on_source=false,eligible=false;
    int usable_pads=0,approach=999;
};
struct Proposal {
    int tile=0,holder=-1,outside=-1,approach=999,pads=0,capture=5;
    bool fresh_witness=false,visible_eligible_holder=false,outside_viable=false;
};
enum class Kind {solo,holding,approaching,outside,banking};
struct Dispatch {Kind kind=Kind::solo;int tile=0,outside=-1;};

// The SDK adapter and mutable-world fixtures execute this same dispatch.
// Adapter operations are read(), write_if(expected,next,owner_round),
// observe(packet), approach(canonical_tile), depart(canonical_tile,outside_id).
// write_if must reserve fuel, reread the whole live word, check the owner round,
// and write the whole replacement without expensive work between those calls.
class Controller {
    Packet active_{},retired_{},newest_{};
    bool active_valid_=false,retired_valid_=false,income_=false,upgrade_=false;
    bool debt_=false,paid_pending_=false,bank_due_=false,own_source_paid_=false;
    int outside_=-1,donated_tile_=0,own_type_=-1,newest_offer_=-1;
    void retire(const Packet&p) {
        retired_=p;retired_valid_=true;active_valid_=false;paid_pending_=false;own_source_paid_=false;
    }
    bool retired(const Packet&p)const{return retired_valid_&&same_key(retired_,p);}
    template<class Adapter>void cancel(Adapter&a,std::uint64_t word,Packet p,int round) {
        p.phase=Phase::cancel;
        if(round>=p.offer&&round-p.offer<32)p.report=static_cast<std::uint8_t>(round-p.offer);
        a.write_if(word,encode(p),round);retire(p);
    }
    Dispatch continuation()const {
        if(bank_due_)return {Kind::banking};
        if(debt_&&outside_>=0)return {Kind::outside,donated_tile_,outside_};
        return {};
    }
public:
    int offers=0,acceptances=0,departures=0,releases=0,holder_payouts=0,outside_payouts=0,banks=0;
    bool funded()const{return income_&&upgrade_&&!debt_;}
    bool donation_debt()const{return debt_;}
    bool needs_bank()const{return bank_due_;}
    int preferred_outside()const{return debt_?outside_:-1;}
    const Packet&active_packet()const{return active_;}
    void purchased(){upgrade_=true;}
    void banked(){if(bank_due_)++banks;bank_due_=false;}
    void died() {
        if(active_valid_)retire(active_);
        active_valid_=false;paid_pending_=false;bank_due_=false;own_source_paid_=false;
        // Equipment survives death. Old income cannot fund a new donation;
        // an already donated capture still requires fresh outside income.
        income_=false;
    }
    bool sync_release(std::uint64_t word,int round) {
        const auto p=decode(word);
        if(!active_valid_||own_type_!=active_.holder||
            (active_.phase!=Phase::accept&&active_.phase!=Phase::released)||
            !same_key(active_,p)||retired(p)||!valid(p,round)||round>p.deadline||
            p.phase!=Phase::released)return false;
        active_=p;return true;
    }
    void own_payout(int tile,int own_bag=1) {
        if(tile<=0||tile>65535)return;
        income_=true;
        if(debt_&&tile!=donated_tile_){debt_=false;outside_=-1;++outside_payouts;}
        if(!active_valid_||tile!=active_.tile)return;
        if(active_.phase==Phase::released&&own_type_==active_.holder){paid_pending_=true;bank_due_=own_bag>0;++holder_payouts;}
        else own_source_paid_=true; // Shared payment before departure is no full transfer.
    }
    template<class Adapter>bool offer(Adapter&a,const Proposal&proposal) {
        if(!funded()||active_valid_||proposal.tile<=0||proposal.tile>65535||
            !physical_type(proposal.holder)||proposal.holder==10||proposal.outside<0||
            !proposal.fresh_witness||!proposal.visible_eligible_holder||!proposal.outside_viable||
            proposal.pads<2||proposal.capture<3||proposal.capture>28||
            proposal.approach<0||proposal.approach>proposal.capture-3)return false;
        Packet p;p.tile=static_cast<std::uint16_t>(proposal.tile);p.clearer=10;
        p.holder=static_cast<std::uint8_t>(proposal.holder);p.phase=Phase::offer;
        const auto o=a.observe(p);
        own_type_=o.type;
        if(o.type!=10||!o.alive||o.channel||!o.source_valid||!o.reward||!o.on_source||
            o.renewed||o.usable_pads<2||o.round<0||o.round+proposal.capture+2>65535||
            o.capture!=proposal.capture)return false;
        p.offer=static_cast<std::uint16_t>(o.round);
        p.deadline=static_cast<std::uint16_t>(o.round+proposal.capture+2);
        const auto word=a.read();const auto old=decode(word);
        if(word!=0&&valid(old,o.round)&&o.round<=old.deadline&&
            old.phase!=Phase::paid&&old.phase!=Phase::cancel)return false;
        if(!a.write_if(word,encode(p),o.round))return false;
        active_=p;active_valid_=true;newest_=p;newest_offer_=p.offer;
        outside_=proposal.outside;own_source_paid_=false;++offers;
        return true;
    }
    template<class Adapter>Dispatch dispatch(Adapter&a) {
        const auto word=a.read();Packet p=decode(word);
        const auto o=a.observe(p);
        own_type_=o.type;
        if(!o.alive) {
            if(word!=0&&valid(p,o.round)&&o.source_valid&&(o.type==p.clearer||o.type==p.holder)) {
                if(p.phase!=Phase::paid&&p.phase!=Phase::cancel) {
                    auto canceled=p;canceled.phase=Phase::cancel;
                    if(o.round-p.offer<32)canceled.report=static_cast<std::uint8_t>(o.round-p.offer);
                    a.write_if(word,encode(canceled),o.round);
                }
                retire(p);
                if(p.offer>=newest_offer_){newest_=p;newest_offer_=p.offer;}
            }
            died();return {};
        }
        if(word==0)return continuation();
        if(!valid(p,o.round)||!o.source_valid)return continuation();
        const bool clearer=o.type==p.clearer,holder=o.type==p.holder;
        if(!clearer&&!holder)return continuation();
        if(retired(p))return continuation();
        if(p.offer<newest_offer_||(p.offer==newest_offer_&&!same_key(newest_,p)))return continuation();
        const bool known=active_valid_&&same_key(active_,p);
        // RELEASED is meaningful only to a holder that accepted this exact
        // key. A late packet is not permission to adopt an unrelated promise.
        if(holder&&p.phase==Phase::released&&!known)return continuation();
        if(known&&active_.phase==Phase::released&&p.phase<Phase::released)return continuation();
        newest_=p;newest_offer_=p.offer;
        if(active_valid_&&!same_key(active_,p)) {
            // A new bus key cannot revive the previous commitment.
            retire(active_);
        }
        if(p.phase==Phase::cancel||p.phase==Phase::paid) {retire(p);return continuation();}
        if(o.round>p.deadline) {cancel(a,word,p,o.round);return continuation();}
        if(holder&&paid_pending_&&active_valid_&&same_key(active_,p)&&p.phase==Phase::released) {
            p.phase=Phase::paid;p.report=static_cast<std::uint8_t>(o.round-p.offer);
            if(a.write_if(word,encode(p),o.round))retire(p);
            return continuation();
        }
        if(o.renewed||own_source_paid_) {cancel(a,word,p,o.round);return continuation();}
        active_=p;active_valid_=true;
        if(clearer) {
            if(p.phase==Phase::released)return continuation();
            if(!o.on_source||!o.reward||o.channel||o.usable_pads<2||
                o.capture<3||o.round-p.offer>o.capture-2) {
                cancel(a,word,p,o.round);return continuation();
            }
            if(p.phase!=Phase::accept||o.round-(p.offer+p.report)>1||!o.holder_visible_on_source)
                return {Kind::holding,p.tile};
            a.depart(p.tile,outside_);
            const auto after=a.observe(p);
            // A legal step within a long source is not a departure. Recheck the
            // physical observation and live word after routing has consumed fuel.
            if(after.round!=o.round||!after.alive||after.on_source)return {Kind::holding,p.tile};
            debt_=true;donated_tile_=p.tile;++departures;
            Packet release=p;release.phase=Phase::released;
            release.report=static_cast<std::uint8_t>(o.round-p.offer);
            if(after.reward&&!after.renewed&&after.holder_visible_on_source&&
                a.write_if(word,encode(release),o.round)){active_=release;++releases;}
            else retire(p);
            return continuation();
        }
        if(p.phase==Phase::released) {
            // The holder's independent CaptureWatch bounds a hidden contest.
            // Contract cancellation deliberately has no access to that watch.
            if(o.on_source&&o.reward&&!o.channel)return {Kind::holding,p.tile};
            cancel(a,word,p,o.round);return continuation();
        }
        const int age=o.round-p.offer;
        if(o.channel||o.bag>0||o.hp<o.maximum*.65f||!o.eligible||o.usable_pads<2||
            o.capture<3||age>o.capture-2||o.approach<0||o.approach>o.capture-age-2) {
            cancel(a,word,p,o.round);return continuation();
        }
        p.phase=Phase::accept;p.report=static_cast<std::uint8_t>(age);
        if(!a.write_if(word,encode(p),o.round))return continuation();
        if(active_.phase!=Phase::accept)++acceptances;
        active_=p;
        if(!o.on_source)a.approach(p.tile);
        return {o.on_source?Kind::holding:Kind::approaching,p.tile};
    }
};

// Own financial/lifecycle receipt, sampled before shopping and saved after
// actions. Neither a kill nor a teammate report can become personal income.
struct Receipts {
    int bag=0,gold=0,held=-1,submit_round=-1,submit_bag=0,submit_ticks=0;
    bool initialized=false,alive=true,continuous=false,reward=false;
    struct Gain {bool payout=false,bank=false;int source=-1;};
    Gain sample(int round,int now_bag,int now_gold,bool living,bool submitting,
        int on_source,bool reward_available,bool respawning) {
        Gain g;
        if(initialized) {
            g.source=held;
            g.payout=living&&alive&&continuous&&reward&&held>=0&&on_source==held&&respawning&&submit_round<0&&
                (now_bag>bag||now_gold>gold);
            g.bank=living&&alive&&submit_round>=0&&round>=submit_round+submit_ticks&&
                !submitting&&now_bag<bag&&submit_bag>0;
        }
        if(!living||!alive){submit_round=-1;continuous=false;reward=false;held=-1;}
        if(!submitting&&submit_round>=0&&!g.bank&&round>submit_round)submit_round=-1;
        if(g.bank)submit_round=-1;
        held=living?on_source:-1;continuous=living&&on_source>=0;
        reward=living&&reward_available;
        bag=now_bag;gold=now_gold;alive=living;initialized=true;
        return g;
    }
    void after(int now_bag,int now_gold,bool living,int on_source,bool reward_available) {
        bag=now_bag;gold=now_gold;alive=living;held=living?on_source:-1;
        continuous=living&&on_source>=0;reward=living&&reward_available;
    }
    void submit(int round,int gems,double multiplier) {
        submit_round=round;submit_bag=gems;submit_ticks=std::max(1,int(std::ceil(gems*multiplier)));
    }
};
template<class Walk,class Work>bool leave_or_work(Walk walk,Work work){return walk()||work();}
} // namespace h12_witnessed_handoff
