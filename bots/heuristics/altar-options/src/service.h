#pragma once
#include <algorithm>
#include <array>

namespace h12_altar_options {
// The adapter supplies only two public, walk-reachable options when committing.
// `travel` is remaining ordinary steps to tribute range. A hidden observation
// has hits=-1; visibility, never friendly-lock presence, establishes zero hits.
struct Altar {
    int id=-1,travel=10000,hits=-1;
    bool visible=false,in_attack=false,in_submit=false,reachable=false;
};
enum class Phase {idle,inspect,remove,submit,confirm};

// A private cargo service, not a map planner. World supplies round/bag/score,
// alive/channeling/submitting/ranged, options()->array<Altar,2>, observe(id),
// unsafe(id), can_attack/reset_ready and checked walk/hit/reset/submit actions.
// Each hit() must use a fresh hostile snapshot and actual allowance. submit()
// returns true only for an actual submitting channel. Optional event(name,id,n)
// records capped diagnostics. tick() true owns this turn; false permits normal
// production or recovery. No banking request can trap an empty bag.
class Service {
    Phase phase_=Phase::idle;
    std::array<int,2> ids_{-1,-1};
    int target_=-1,deadline_=0,progress_=0,best_travel_=10000;
    int last_hits_=-1,failed_=0,cooldown_=0,channel_bag_=0,channel_score_=0;
    int alternate_hits_=-2;
    unsigned failed_options_=0;
    bool alternate_visible_=false;

    template<class W> static void event(W&w,const char*name,int id,int value) {
        if constexpr(requires{w.event(name,id,value);})w.event(name,id,value);
    }
    template<class W> void abandon(W&w,const char*why) {
        event(w,why,target_,w.bag());
        phase_=Phase::idle;target_=-1;ids_={-1,-1};failed_=0;
        cooldown_=w.round()+8;channel_bag_=0;
    }
    static bool usable(const Altar&a) {return a.id>=0&&a.reachable&&a.travel<10000;}
    template<class W> int cost(W&w,const Altar&a) const {
        if(!usable(a)||w.unsafe(a.id))return 100000;
        for(unsigned i=0;i<ids_.size();++i)
            if(ids_[i]==a.id&&(failed_options_&(1u<<i)))return 100000;
        // A hidden altar is an arrival/inspection option, not a claimed empty
        // altar. It must be observed again before any lock or channel action.
        if(!a.visible||a.hits<0)return a.travel+1;
        const int hits=std::max(0,a.hits);
        if(!hits)return a.travel;
        // Count successful basics, never Strength damage. A currently ready
        // reset is one extra basic at most, sharing the single cast allowance.
        // Ranged arrival walking leaves no ordinary basic on that same turn.
        const int first=a.in_attack?(int(w.can_attack())+int(w.reset_ready())):
            (int(!w.ranged())+int(w.reset_ready()));
        return a.travel+std::max(0,hits-first)+(a.in_attack?0:1);
    }
    template<class W> void commit(W&w,const Altar&a,bool switched) {
        target_=a.id;phase_=a.visible?(a.hits>0?Phase::remove:Phase::submit):Phase::inspect;
        best_travel_=a.travel;last_hits_=a.visible?a.hits:-1;progress_=w.round();failed_=0;
        event(w,switched?"divert":"inspect",target_,a.visible?a.hits:-1);
    }
    template<class W> bool reconsider(W&w,const Altar&current,const Altar&alternate,bool force) {
        if(failed_>=3)for(unsigned i=0;i<ids_.size();++i)
            if(ids_[i]==current.id)failed_options_|=1u<<i;
        const int old_cost=cost(w,current),other_cost=cost(w,alternate);
        // Previous hits are sunk. Only current remaining work enters the
        // choice; a small switching cost prevents needless alternate churn.
        if(usable(alternate)&&other_cost+2<old_cost) {
            commit(w,alternate,true);return true;
        }
        if(force&&(old_cost>=100000||failed_>=3)) {
            if(usable(alternate)&&other_cost<100000) {commit(w,alternate,true);return true;}
            abandon(w,"refuse");return false;
        }
        return true;
    }
public:
    Phase phase()const{return phase_;}
    int target()const{return target_;}
    int deadline()const{return deadline_;}
    unsigned confirmed=0;
    bool active()const{return phase_!=Phase::idle;}

    // Non-acting lifecycle observation may run before the executor's capture
    // and recovery priorities. It never walks, attacks, casts or submits.
    template<class W> void sync(W&w) {
        if(!w.alive()) {
            if(active())abandon(w,"death");
            return;
        }
        // Preserve both service and pre-existing external channels. A bag
        // change while that channel is active is not a reason to cancel it.
        if(w.channeling())return;
        if(phase_==Phase::confirm) {
            if(w.bag()==0) {
                if(channel_bag_>0&&w.score()-channel_score_>=channel_bag_) {
                    ++confirmed;event(w,"confirmed",target_,channel_bag_);
                } else event(w,"unconfirmed",target_,channel_bag_);
                phase_=Phase::idle;target_=-1;ids_={-1,-1};channel_bag_=0;
                return;
            }
            // Stun or a new lock may cancel a channel without consuming cargo.
            phase_=Phase::inspect;event(w,"interrupted",target_,w.bag());
        }
        if(w.bag()<=0) {
            if(active())abandon(w,"empty");
            return;
        }
    }

    template<class W> bool tick(W&w,bool requested) {
        sync(w);
        const int now=w.round();
        if(!w.alive()||w.bag()<=0)return w.alive()&&w.channeling();
        if(w.channeling())return true;
        if(!active()) {
            if(!requested||now<cooldown_)return false;
            const auto options=w.options();
            ids_={options[0].id,options[1].id};
            failed_options_=0;
            const int first=cost(w,options[0]),second=cost(w,options[1]);
            const Altar chosen=second<first?options[1]:options[0];
            if(cost(w,chosen)>=100000) {cooldown_=now+8;return false;}
            int tribute=1;
            if constexpr(requires{w.tribute_ticks();})tribute=std::max(1,w.tribute_ticks());
            deadline_=now+std::clamp(18+chosen.travel*3+tribute,18,120);
            commit(w,chosen,chosen.id!=options[0].id);
            const int other=ids_[0]==target_?ids_[1]:ids_[0];
            if(other>=0) {const auto a=w.observe(other);alternate_visible_=a.visible;alternate_hits_=a.hits;}
            else {alternate_visible_=false;alternate_hits_=-2;}
        }
        if(now>=deadline_) {abandon(w,"expiry");return false;}
        auto current=w.observe(target_);
        const int other=ids_[0]==target_?ids_[1]:ids_[0];
        auto alternate=other>=0?w.observe(other):Altar{};
        const bool new_lock=current.visible&&current.hits>0&&
            (last_hits_<0||current.hits>last_hits_);
        const bool alternate_changed=alternate.visible&&
            (!alternate_visible_||alternate.hits!=alternate_hits_);
        const bool unsafe=w.unsafe(target_);
        if(current.travel<best_travel_||(current.visible&&last_hits_>=0&&current.hits<last_hits_))progress_=now;
        best_travel_=std::min(best_travel_,current.travel);
        const bool stagnant=now-progress_>=6;
        if(new_lock||alternate_changed||unsafe||stagnant||failed_>=3||!usable(current)) {
            const int old=target_;
            if(!reconsider(w,current,alternate,unsafe||failed_>=3||!usable(current)))return false;
            if(target_!=old) {current=w.observe(target_);alternate=w.observe(old);}
            else if(stagnant) {abandon(w,"stagnant");return false;}
        }
        alternate_visible_=alternate.visible;alternate_hits_=alternate.hits;
        last_hits_=current.visible?current.hits:-1;
        bool tried_walk=false;
        if(!current.visible||(!current.in_attack&&!current.in_submit)) {
            phase_=Phase::inspect;
            tried_walk=true;
            if(w.walk(target_)) {failed_=0;current=w.observe(target_);}
            else ++failed_;
            if(!current.visible)return true;
        }
        if(current.hits>0) {
            phase_=Phase::remove;
            // At most two basics and one reset; no open-ended action loop.
            for(int n=0;n<2;++n) {
                current=w.observe(target_);
                if(!current.visible||current.hits<=0||!current.in_attack)break;
                if(!w.can_attack()) {
                    if(n>0||!w.reset_ready()||!w.reset())break;
                }
                if(!w.can_attack())break;
                const int before=current.hits;
                if(!w.hit(target_)) {++failed_;break;}
                const auto fresh=w.observe(target_);
                if(fresh.visible&&fresh.hits<before) {
                    event(w,"hit",target_,fresh.hits);progress_=now;failed_=0;
                    last_hits_=fresh.hits;
                } else {++failed_;break;}
                // The second basic requires the remaining single reset. The
                // adapter's actual can_cast prevents two successful casts.
                if(n==0&&fresh.hits>0&&!w.can_attack()&&w.reset_ready())w.reset();
            }
            // Do not rely on final-hit and submit sharing one owner turn.
            return true;
        }
        if(!current.visible||current.hits<0)return true;
        if(!current.in_submit) {
            phase_=Phase::inspect;
            if(!tried_walk) {if(w.walk(target_))failed_=0;else ++failed_;}
            return true;
        }
        phase_=Phase::submit;
        const int bag=w.bag(),score=w.score();
        if(w.submit(target_)&&w.submitting()) {
            phase_=Phase::confirm;channel_bag_=bag;channel_score_=score;
            event(w,"channel",target_,bag);return true;
        }
        // A locked tribute may silently become a basic. It is not a channel.
        current=w.observe(target_);last_hits_=current.visible?current.hits:-1;
        phase_=Phase::inspect;++failed_;
        return true;
    }
};
} // namespace h12_altar_options
