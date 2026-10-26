#pragma once
#include "config.h"
#if RESET_MENU_DIAGNOSTIC
#include <array>
#include <cstdio>
#include <span>

namespace h12_reset_pursuit {
struct MenuDiagnostic {
    struct Lease { int current=-1, progress=0, travel=0; float hp=0; };
    struct Ranked {
        int id=-1; double finish=0, cash=0, value=0;
        bool cashable=false, quick=false; std::array<double,4> key{};
    };
    struct Selection {
        bool used=false, partial=false, lease_return=false, has_cash=false, has_quick=false;
        int supplied=0, admitted=0, ranked=0, returned=-1, best=-1;
        Lease before{}, after{};
        std::array<Ranked,12> ranks{};
    };
    struct Scan { int id=-1, x=0, y=0, stage=0; };
    struct Frame {
        bool active=false, captured=false, overflow=false, budget_break=false, choose_called=false;
        int round=0, finish_round=0, role=0, groups=0, eligible=0, visited=0, choice=-1, target=-1;
        int fuel_begin=0, fuel_before_choose=0, fuel_after_choose=0, fuel_finish=0, copy_end_round=0, copy_end_fuel=0;
        int scan_count=0, job_count=0, selections=0, eligible_recorded=0;
        heuristics::State state{};
        Lease before{}, after{};
        std::array<Scan,64> eligible_sources{};
        std::array<Scan,12> scan{};
        std::array<heuristics::Job,12> jobs{};
        std::array<Selection,2> selection{};
    } frame;
    int active_selection=-1, emitted=0;
    bool flushed=false;

    bool wants(int round,int role) const {
        return !frame.captured && !frame.active && round>0 && round<20
            && role==RESET_MENU_DIAGNOSTIC_ROLE;
    }
    bool begin(int round,int role,int groups,int fuel,Lease lease) {
        if(!wants(round,role))return false;
        frame.active=true;frame.round=round;frame.role=role;frame.groups=groups;
        frame.fuel_begin=fuel;frame.before=lease;return true;
    }
    void capture_eligible(std::span<const heuristics::Detail> detail) {
        if(!frame.active)return;
        frame.eligible_recorded=int(std::min(detail.size(),frame.eligible_sources.size()));
        frame.overflow|=detail.size()>frame.eligible_sources.size();
        for(int i=0;i<frame.eligible_recorded;++i)frame.eligible_sources[i].id=detail[i].id;
    }
    void open_selection(bool partial,int supplied,Lease before) {
        if(!frame.active)return;
        if(frame.selections>=int(frame.selection.size())){frame.overflow=true;active_selection=-1;return;}
        active_selection=frame.selections++;
        auto&r=frame.selection[active_selection];
        r.used=true;r.partial=partial;r.supplied=supplied;r.before=before;
    }
    void admission(int count,bool cash,bool quick) {
        if(active_selection<0||!frame.active)return;
        auto&r=frame.selection[active_selection];
        r.admitted=count;r.has_cash=cash;r.has_quick=quick;
    }
    void rank(int id,double finish,double cash,double value,bool cashable,bool quick,
              const std::array<double,4>&key) {
        if(active_selection<0||!frame.active)return;
        auto&r=frame.selection[active_selection];
        if(r.ranked>=int(r.ranks.size())){frame.overflow=true;return;}
        r.ranks[r.ranked++]={id,finish,cash,value,cashable,quick,key};
    }
    void returned(int id,int best,bool lease,Lease after) {
        if(active_selection<0||!frame.active)return;
        auto&r=frame.selection[active_selection];
        r.returned=id;r.best=best;r.lease_return=lease;r.after=after;
    }
    template<class Groups>
    void finish(const heuristics::State&state,std::span<const heuristics::Job>jobs,
                std::span<const int>order,const std::array<int,12>&stages,int eligible,int visited,
                bool budget_break,int choice,int target,Lease after,int finish_round,int fuel,
                const Groups&groups) {
        if(!frame.active)return;
        frame.state=state;frame.eligible=eligible;frame.visited=visited;
        frame.budget_break=budget_break;frame.choice=choice;frame.target=target;
        frame.choose_called=frame.selections>0;frame.after=after;
        frame.finish_round=finish_round;frame.fuel_finish=fuel;
        frame.scan_count=int(std::min(order.size(),frame.scan.size()));
        frame.job_count=int(std::min(jobs.size(),frame.jobs.size()));
        frame.overflow|=order.size()>frame.scan.size()||jobs.size()>frame.jobs.size();
        for(int i=0;i<frame.eligible_recorded;++i){
            auto&r=frame.eligible_sources[i];const auto&anchor=groups[r.id].anchor;
            r.x=anchor.x;r.y=anchor.y;
        }
        for(int i=0;i<frame.scan_count;++i){
            const int id=order[i];const auto&anchor=groups[id].anchor;
            frame.scan[i]={id,anchor.x,anchor.y,stages[i]};
        }
        for(int i=0;i<frame.job_count;++i)frame.jobs[i]=jobs[i];
        frame.active=false;frame.captured=true;
    }
    int line_count()const {
        int result=6+frame.eligible_recorded+frame.scan_count+frame.job_count;
        for(int i=0;i<frame.selections;++i)result+=1+frame.selection[i].ranked;
        return result;
    }
    void emit_line(int line,int emit_round,int emit_fuel)const {
        const auto&f=frame;
        if(line==0){std::printf("resetmenu header role=%d round=%d finish=%d groups=%d eligible=%d eligible_kept=%d visited=%d scans=%d jobs=%d selects=%d choice=%d target=%d budget=%d overflow=%d choose=%d copy_end=%d emit_round=%d emit_fuel=%d\n",
            f.role,f.round,f.finish_round,f.groups,f.eligible,f.eligible_recorded,f.visited,f.scan_count,f.job_count,f.selections,f.choice,f.target,int(f.budget_break),int(f.overflow),int(f.choose_called),f.copy_end_round,emit_round,emit_fuel);return;}
        if(line==1){const auto&s=f.state;std::printf("resetmenu state round=%d limit=%d bag=%d gold=%d altar=%d identity=%d capture=%d recall=%d monster=%d jungle=%d mature=%d hp=%a max=%a tribute=%a\n",
            s.round,s.limit,s.bag,s.gold,s.altar,s.identity,s.capture,s.recall,s.monster,s.jungle,int(s.mature),double(s.hp),double(s.maximum),s.tribute);return;}
        if(line==2){std::printf("resetmenu fuel begin=%d beforechoose=%d afterchoose=%d copy_entry=%d copy_end=%d\n",f.fuel_begin,f.fuel_before_choose,f.fuel_after_choose,f.fuel_finish,f.copy_end_fuel);return;}
        if(line==3){std::printf("resetmenu lease before_current=%d before_progress=%d before_travel=%d before_hp=%a after_current=%d after_progress=%d after_travel=%d after_hp=%a\n",
            f.before.current,f.before.progress,f.before.travel,double(f.before.hp),f.after.current,f.after.progress,f.after.travel,double(f.after.hp));return;}
        line-=4;
        if(line<f.eligible_recorded){const auto&r=f.eligible_sources[line];std::printf("resetmenu eligible n=%d id=%d x=%d y=%d\n",line,r.id,r.x,r.y);return;}
        line-=f.eligible_recorded;
        if(line<f.scan_count){const auto&r=f.scan[line];std::printf("resetmenu scan n=%d id=%d x=%d y=%d stage=%d\n",line,r.id,r.x,r.y,r.stage);return;}
        line-=f.scan_count;
        if(line<f.job_count){const auto&j=f.jobs[line];std::printf("resetmenu job n=%d id=%d travel=%d altar=%d allies=%d enemy=%d gems=%d gold=%d home=%d age=%d work=%a loss=%a incoming=%a original=%a remaining=%a ready=%d known=%d committed=%d affordable=%d\n",
            line,j.id,j.travel,j.altar,j.allies,j.enemy_eta,j.gems,j.gold,j.home,j.age,double(j.work),double(j.loss),double(j.incoming),double(j.original_hp),double(j.remaining_hp),int(j.ready),int(j.known),int(j.committed),int(j.affordable));return;}
        line-=f.job_count;
        for(int i=0;i<f.selections;++i){const auto&r=f.selection[i];
            if(line==0){std::printf("resetmenu select n=%d partial=%d supplied=%d admitted=%d hascash=%d hasquick=%d ranked=%d returned=%d best=%d lease=%d before_current=%d after_current=%d\n",
                i,int(r.partial),r.supplied,r.admitted,int(r.has_cash),int(r.has_quick),r.ranked,r.returned,r.best,int(r.lease_return),r.before.current,r.after.current);return;}
            --line;
            if(line<r.ranked){const auto&m=r.ranks[line];std::printf("resetmenu rank select=%d n=%d id=%d finish=%a cash=%a value=%a cashable=%d quick=%d late=%a tier=%a cost=%a tie=%a\n",
                i,line,m.id,m.finish,m.cash,m.value,int(m.cashable),int(m.quick),m.key[0],m.key[1],m.key[2],m.key[3]);return;}
            line-=r.ranked;
        }
        if(line==0){std::printf("resetmenu end role=%d captured_round=%d lines=%d emit_round=%d emit_fuel=%d\n",f.role,f.round,line_count(),emit_round,emit_fuel);return;}
        std::printf("resetmenu final role=%d captured_round=%d emit_round=%d emit_fuel=%d\n",f.role,f.round,emit_round,emit_fuel);
    }
    // One bounded line on a fresh dedicated owner turn, after the entire sample window.
    bool flush(int round,int fuel) {
        if(!frame.captured||flushed||round<=20||fuel<90000)return false;
        emit_line(emitted++,round,fuel);
        if(emitted>=line_count())flushed=true;
        return true;
    }
};
} // namespace h12_reset_pursuit
#endif
