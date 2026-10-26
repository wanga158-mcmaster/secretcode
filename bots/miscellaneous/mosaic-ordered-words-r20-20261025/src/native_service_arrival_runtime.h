// Included INSIDE Runtime after native_service_facts.h and the parent's native
// state/geometry helpers. File scope includes native_service_arrival.h first.
// This resolver performs paid observations only. It never dispatches, changes a
// retained job, positively publishes NativeFacts, or credits a START/completion/
// payout. Existing native_read_* adapters still reject stale cached facts on a
// failed acquisition; this resolver never masks that safety invalidation.
native_service::ArrivalResult native_resolve_arrival(native_service::Budget& budget,
    const native_service::SelfFact& self_fact,const native_service::ArrivalRequest& request) {
    using namespace native_service;ArrivalResult result{};const auto now=native_facts().epoch;
    result.decision.epoch=now;result.next_work_cursor=request.work_cursor;result.next_bank_cursor=request.bank_cursor;
    auto observe=[&](unsigned needs){ArrivalDecision value{};value.kind=ArrivalKind::observe;value.epoch=now;value.needs=needs;
        result.decision=arrival_prefer(result.decision,value);};
    if(!self_fact.actual(now)){observe(arrival_need_self);return result;}
    if(!arrival_channel_ready(self_fact,now))return result;
    const bool bank_owner=request.purpose==ArrivalPurpose::bank||request.custody_required||
        (self_fact.gems_actual&&self_fact.gems>0);
    if(bank_owner&&request.allow_bank){
        if(!self_fact.gems_actual){observe(arrival_need_custody);return result;}
        if(self_fact.gems>0){
            if(self_fact.team<0||self_fact.team>1){observe(arrival_need_binding);return result;}
            std::array<int,2> tried{{-1,-1}};unsigned tried_count=0;
            auto probe=[&](std::size_t index){
                const auto at=altars[index];
                if(distance(self_fact.pose,{at.x,at.y})>int(constants::submit_gems_range))return false;
                for(unsigned i=0;i<tried_count;++i)if(tried[i]==int(index))return false;
                if(result.lock_probes==2){result.more_banks=true;result.next_bank_cursor=index;
                    if(result.decision.kind==ArrivalKind::none)result.decision.kind=ArrivalKind::observe;
                    result.decision.needs|=arrival_need_lock;return false;}
                tried[tried_count++]=int(index);++result.lock_probes;
                ArrivalBankOffer offer{};offer.epoch=now;offer.altar={at.x,at.y};offer.ordinal=int(index);
                offer.range=constants::submit_gems_range;offer.static_altar=true;
                if(!budget.before_query()){result.query_denied=true;observe(arrival_need_lock|arrival_need_funding);return false;}
                detail::altar_lock_info lock{};
                const auto code=detail::sdk_altar_lock(at.x,at.y,1-self_fact.team,&lock);
                const bool paid=budget.after_query();
                if(code!=int(error_code::api_not_found)&&code!=int(error_code::api_not_in_vision))detail::check(code);
                if(!paid){result.query_denied=true;observe(arrival_need_lock|arrival_need_funding);return false;}
                if(code==int(error_code::api_not_found))offer.lock_actual=offer.unlocked=true;
                else if(code!=int(error_code::api_not_in_vision)){detail::check(code);offer.lock_actual=true;offer.lock_id=lock.handle;}
                result.decision=arrival_prefer(result.decision,arrival_bank(self_fact,offer,now));
                return result.decision.physical();
            };
            if(request.preferred_altar>=0&&std::size_t(request.preferred_altar)<altars.size())
                if(probe(std::size_t(request.preferred_altar)))return result;
            if(result.query_denied)return result;
            // Bounded catalogue traversal, retaining where additional in-range
            // lock candidates still owe an observation next input.
            const auto size=altars.size();const auto start=size?request.bank_cursor%size:0;
            for(std::size_t scanned=0;scanned<std::min<std::size_t>(size,64);++scanned){
                const auto index=(start+scanned)%size;result.next_bank_cursor=(index+1)%size;
                if(probe(index))return result;
                if(result.query_denied||result.more_banks)return result;
            }
            if(size>64){result.scan_incomplete=result.more_banks=true;observe(arrival_need_catalogue);}
        }
        if(!request.transferred_work)return result;
    }else if(bank_owner&&!request.transferred_work){
        observe(arrival_need_custody);return result;
    }
    if(!request.allow_work&&!request.allow_capture)return result;
    if((request.purpose==ArrivalPurpose::exit||request.purpose==ArrivalPurpose::recover)&&!request.transferred_work)return result;
    std::array<int,2> tried{{-1,-1}};unsigned tried_count=0;
    auto possible=[&]<class Owner>(Owner& owner,int ordinal){
        if constexpr(requires(Owner& value,int index,Epoch when){value.native_arrival_member_needed(index,when);})
            return owner.native_arrival_member_needed(ordinal,now);
        else return true;
    };
    auto probe=[&](std::size_t index){
        const auto& member=members[index];const Point at{member.pos.x,member.pos.y};
        if(request.preserve_job&&member.group!=request.retained_group)return false;
        if(request.preserve_member&&int(index)!=request.retained_member)return false;
        if(!possible(*this,int(index)))return false;
        if(distance(self_fact.pose,at)>std::max(1,self_fact.attack_range))return false;
        for(unsigned i=0;i<tried_count;++i)if(tried[i]==int(index))return false;
        if(result.camp_probes==2){result.more_work=true;result.next_work_cursor=index;
            if(result.decision.kind==ArrivalKind::none)result.decision.kind=ArrivalKind::observe;
            result.decision.needs|=arrival_need_life;return false;}
        tried[tried_count++]=int(index);++result.camp_probes;
        CampFact camp{};
        if(!native_read_camp(budget,int(index),camp)){
            result.query_denied=true;observe(arrival_need_life|arrival_need_funding);return false;
        }
        result.camps[result.camp_count++]=camp;
        GroupFact group{};CaptureFact capture{};
        if(camp.actual(now)&&camp.life==Life::dead){
            if(!native_read_group(budget,camp.group_ordinal,group,int(index))){
                result.query_denied=true;observe(arrival_need_group|arrival_need_funding);return false;
            }
            result.groups[result.group_count++]=group;
            native_capture_at(self_fact,group,int(index),capture);
            if(group.capturable(now)&&!capture.holding_reward(now)&&group.ordinal>=0&&std::size_t(group.ordinal)<memory.size()){
                const auto& footprint=memory[std::size_t(group.ordinal)];
                for(auto member=footprint.begin;member<footprint.end&&member<footprint.begin+64;++member){
                    CaptureFact at{};native_capture_at(self_fact,group,int(member),at);
                    if(at.holding_reward(now)){capture=at;break;}
                }
                if(footprint.end-footprint.begin>64&&!capture.holding_reward(now))result.scan_incomplete=true;
            }
        }
        result.decision=arrival_prefer(result.decision,arrival_work(self_fact,camp,group,capture,now,
            request.allow_work,request.allow_capture));
        return result.decision.physical();
    };
    for(const auto ordinal:std::array<int,2>{request.retained_member,request.blocker_member}){
        if(ordinal>=0&&std::size_t(ordinal)<members.size()){
            if(probe(std::size_t(ordinal)))return result;
            if(result.query_denied||result.more_work)return result;
        }
    }
    // Optional static cell lookup changes acquisition cost, never freshness or
    // authority. The generic lambda keeps runtimes without this hook on their
    // existing catalogue path. A supplied readiness guard must explicitly
    // qualify setup before unknown/unconfigured cells can mean no nomination.
    const bool indexed=[&]<class Owner>(Owner& owner){
        if constexpr(requires(Owner& value,Point point){value.native_arrival_member_at(point);}){
            if constexpr(requires(Owner& value){value.native_arrival_index_ready();})
                if(!owner.native_arrival_index_ready())return false;
            const auto radius=std::uint64_t(std::max(0,self_fact.attack_range));
            const auto side=2*radius+1;
            // A malformed range must not overflow a 32-bit WASM cursor or turn
            // unsupported geometry into an empty-world claim. Retain the old
            // bounded catalogue fallback in that case.
            if(side>std::uint64_t(std::numeric_limits<std::size_t>::max())/side)return false;
            const auto cells=std::size_t(side*side),start=request.work_cursor%cells;
            const auto left=std::int64_t(self_fact.pose.x)-std::int64_t(radius);
            const auto top=std::int64_t(self_fact.pose.y)-std::int64_t(radius);
            for(std::size_t visited=0;visited<std::min<std::size_t>(cells,64);++visited){
                const auto cell=(start+visited)%cells;result.next_work_cursor=(cell+1)%cells;
                if((visited&7u)==0){
                    if(!budget.checkpoint()||budget.pure_used()+budget.pure_bound/2>budget.pure_bound){
                        result.next_work_cursor=cell;result.more_work=result.scan_incomplete=true;
                        observe(arrival_need_catalogue|arrival_need_funding);return true;
                    }
                }
                const auto x=left+std::int64_t(cell%side),y=top+std::int64_t(cell/side);
                if(x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()||
                   y<std::numeric_limits<int>::min()||y>std::numeric_limits<int>::max())continue;
                const auto ordinal=owner.native_arrival_member_at(Point{int(x),int(y)});
                if(ordinal<0)continue;
                if(std::size_t(ordinal)>=members.size()){observe(arrival_need_binding);continue;}
                if(probe(std::size_t(ordinal)))return true;
                // probe's legacy cursor names a catalogue ordinal. This branch
                // instead owes the unprobed/unfunded rectangle cell next input.
                if(result.query_denied||result.more_work){result.next_work_cursor=cell;return true;}
            }
            if(cells>64){result.scan_incomplete=result.more_work=true;observe(arrival_need_catalogue);}
            return true;
        }else return false;
    }(*this);
    if(indexed)return result;
    const auto size=members.size(),start=size?request.work_cursor%size:0;
    for(std::size_t scanned=0;scanned<std::min<std::size_t>(size,64);++scanned){
        const auto index=(start+scanned)%size;result.next_work_cursor=(index+1)%size;
        // Catalogue enrichment cannot consume the mandatory dispatch/route
        // continuation's whole-lane control allowance. Preserve its cursor;
        // retained/blocker physical incumbents were examined before this scan.
        if((scanned&7u)==0){
            if(!budget.checkpoint()||budget.pure_used()+budget.pure_bound/2>budget.pure_bound){
                result.next_work_cursor=index;result.more_work=result.scan_incomplete=true;
                observe(arrival_need_catalogue|arrival_need_funding);return result;
            }
        }
        if(probe(index))return result;
        if(result.query_denied||result.more_work)return result;
    }
    if(size>64){result.scan_incomplete=result.more_work=true;observe(arrival_need_catalogue);}
    return result;
}
