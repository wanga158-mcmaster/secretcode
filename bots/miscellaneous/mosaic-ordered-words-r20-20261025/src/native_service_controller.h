// Included inside Runtime. The complete native lane never calls old planners.
struct NativeServiceState {
    native_service::StationaryIndex<> stationary{};bool stationary_valid{true};
    std::array<native_service::FrontierMember,16384> frontier_members{};
    native_service::GroupFrontier<> frontier{};
    native_service::CycleGroupWork<16384> group_work{};
    std::uint64_t group_generation{1},board_generation{1},member_binding_serial{};
    bool group_goal{};
    native_service::ExecutionState execution{};
    native_service::EgressState egress{};
    native_service::CycleState cycle{};
    native_service::ExitWitness exit_witness{};bool exit_witness_retained{};
    std::uint64_t batch_owner{};bool batch_next_input_exit{};
    int capture_group{-1},capture_deadline{-1};bool capture_reobserve{};
    unsigned rank_decreases{},work_debts{},egress_closures{},coverage_transfers{},restorations{};
    native_service::RouteOwner route{};
    std::uint64_t route_tokens{},funding_serial{},rejection_serial{},service_event{},command_owner{},bank_started_owner{};
    std::size_t arrival_work_cursor{},arrival_bank_cursor{};
    int blocker_member{-1},blocker_round{-1};unsigned move_reason{};
    native_service::Point validation_point{};
    native_service::ServiceState service{};
    native_service::ServiceFields<> fields{};
    native_service::ServiceField active_field{native_service::ServiceField::service};
    std::array<native_service::Point,256> egress_roots{};
    unsigned egress_root_count{};bool egress_roots_prepared{};
    std::array<std::uint8_t,16384> passable{};
    std::array<native_service::RouteCell,16384> camp_routes{};
    alignas(16) std::array<std::uint64_t,256> camp_mask_raw{};
    std::array<std::uint8_t,2048> camp_mask_linear{};
    native_service::Epoch camp_mask_epoch{};
    std::size_t camp_mask_cursor{};bool camp_mask_available{},camp_mask_requested{};
    std::array<std::size_t,16> board_edits{};unsigned board_edit_count{};
    std::array<unsigned,256> route_group_life{};
    std::array<native_service::Epoch,256> route_group_transition{};
    std::array<std::size_t,256> route_clear_cursor{};
    std::array<bool,256> route_clear_pending{};
    std::array<int,256> unavailable_until{};
    std::array<native_service::Point,256> roots{};
    std::array<native_service::Point,8> moves{},excluded{};
    std::size_t board_cursor{};
    unsigned root_count{},move_count{},excluded_count{},stage{},last_status{};
    std::uint64_t intent{},goal_key{},nav_intent{},last_ack{};
    native_service::Point excluded_pose{-1,-1};
    native_service::Point exit_anchor{};unsigned exit_radius{1},exit_offset{};
    native_service::Point approach_goal{};unsigned approach_cursor{};int approach_range{};
    bool approach_prepared{},bank_roots_materialized{};
    std::uint64_t field_revision{};
    int group{-1},member{-1},bank_altar{-1},input_round{-1},last_gems{};
    bool recovering{},exit_due{},board_ready{};
    unsigned programmes{},work_commands{},move_commands{},cast_commands{},bank_starts{},recall_starts{},prepare_units{},ready_inputs{},unsupported{},refused{};
};
static NativeServiceState& native_service_state(){static constinit NativeServiceState value{};return value;}
void native_stationary_store(native_service::Point point,int ordinal){
    using namespace native_service;auto& s=native_service_state();
    if(ordinal>=0&&std::size_t(ordinal)<s.frontier_members.size())s.frontier_members[std::size_t(ordinal)]={point,ordinal};
    if(!s.stationary_valid)return;
    if(!s.stationary.configured()){
        const auto configured=s.stationary.configure(walls->width(),walls->height());
        if(configured!=StationaryConfigure::configured&&configured!=StationaryConfigure::retained){s.stationary_valid=false;++s.unsupported;return;}
    }
    const auto stored=s.stationary.add(point,ordinal);
    if(stored!=StationaryAdd::added&&stored!=StationaryAdd::duplicate){s.stationary_valid=false;++s.unsupported;}
}
bool native_arrival_index_ready()const {const auto& s=native_service_state();return s.stationary_valid&&s.stationary.configured();}
int native_arrival_member_at(native_service::Point point)const {return native_service_state().stationary.at(point);}
static const native_service::Navigation<>& native_navigation(){
    const auto& s=native_service_state();return s.fields.navigation(s.active_field);
}
static constexpr std::uint64_t native_egress_key(){return 0x0545475245535301ull;}
struct NativeServiceTrace {
    std::array<std::uint32_t,24> stages{};
    std::array<unsigned,8> move_reasons{};unsigned last_move_reason{},last_move_rejection{};
    unsigned overlay_changes{},lifecycle_changes{},approach_changes{};
    native_service::Point rejected_point{},validated_point{};
    std::uint64_t entry{},last{},sdk_spent{},pure{},max_pure{};
    unsigned queries{},commands{};bool poisoned{};
};
static NativeServiceTrace& native_service_trace(){static constinit NativeServiceTrace value{};return value;}
static void native_trace_move_reason(unsigned reason,native_service::Point point){if constexpr(cfg::trace>=12){
    auto& t=native_service_trace();t.last_move_reason=reason;t.validated_point=point;
    if(reason<t.move_reasons.size())++t.move_reasons[reason];
    if(reason>=1&&reason<=4){t.last_move_rejection=reason;t.rejected_point=point;}
}}
struct NativeServiceTraceGuard {
    native_service::Budget& budget;
    ~NativeServiceTraceGuard(){if constexpr(cfg::trace>=12||cfg::trace<0){
        if constexpr(cfg::trace<0)if(static_cast<Runtime*>(budget.context)->epoch!=-cfg::trace)return;
        auto& t=native_service_trace();const auto& s=native_service_state();
        if(s.stage<t.stages.size())++t.stages[s.stage];
        t.entry=budget.entry;t.last=budget.last;t.sdk_spent=budget.sdk_spent;t.pure=budget.pure_used();
        t.max_pure=std::max(t.max_pure,t.pure);t.queries=budget.queries;t.commands=budget.commands;t.poisoned=budget.poisoned;
    }}
};
void native_diagnostic_end(){if constexpr(cfg::trace>=12){
    if(epoch!=32&&epoch!=128&&epoch!=512&&epoch!=1024&&epoch!=1536&&epoch!=1999)return;
    if(!fuel(20000))return;
    const auto& s=native_service_state();const auto& t=native_service_trace();const auto& me=native_facts().self;
    std::array<std::uint32_t,82> packet{};
    packet[0]=0x34534e4du;packet[1]=(4u<<16)|std::uint32_t(packet.size()*sizeof(packet[0]));
    packet[2]=epoch;packet[3]=std::uint32_t(handle_champion_t(self()));
    packet[4]=s.stage;packet[5]=s.last_status;packet[6]=t.entry;packet[7]=t.last;packet[8]=t.sdk_spent;
    packet[9]=t.pure;packet[10]=t.max_pure;packet[11]=t.queries;packet[12]=t.commands;packet[13]=t.poisoned;
    packet[14]=s.ready_inputs;packet[15]=s.board_cursor;packet[16]=s.group;packet[17]=s.member;
    packet[18]=s.programmes;packet[19]=s.work_commands;packet[20]=s.move_commands;packet[21]=s.refused;
    packet[22]=me.pose.x;packet[23]=me.pose.y;packet[24]=me.raw_m;packet[25]=me.alive;packet[26]=me.actual(native_facts().epoch);
    packet[27]=s.execution.pending;packet[28]=s.execution.input_blocked;packet[29]=s.root_count;
    packet[30]=s.move_count;packet[31]=s.recovering;
    for(unsigned i=0;i<t.stages.size();++i)packet[32+i]=t.stages[i];
    packet[56]=unsigned(native_navigation().status(me.pose));packet[57]=unsigned(native_navigation().status());
    packet[58]=unsigned(s.goal_key>>56);packet[59]=s.root_count?s.roots[0].x:0xffffffffu;
    packet[60]=s.root_count?s.roots[0].y:0xffffffffu;packet[61]=native_navigation().distance(me.pose).value_or(-1);
    packet[62]=s.prepare_units;packet[63]=s.exit_radius;
    packet[64]=t.last_move_reason;packet[65]=t.last_move_rejection;
    packet[66]=t.rejected_point.x;packet[67]=t.rejected_point.y;
    for(unsigned i=1;i<=6;++i)packet[67+i]=t.move_reasons[i];
    packet[74]=t.validated_point.x;packet[75]=t.validated_point.y;
    packet[76]=s.field_revision;packet[77]=t.overlay_changes;packet[78]=t.lifecycle_changes;packet[79]=t.approach_changes;
    packet[80]=s.approach_cursor;packet[81]=s.approach_prepared;emit_packet(packet);
}}
static std::uint64_t native_clock(void* context) {
    const auto* owner=static_cast<Runtime*>(context);std::uint32_t actual{};
    detail::check(detail::sdk_bc_round_number(&actual));
    return int(actual)==owner->epoch?fuel_remaining():std::numeric_limits<std::uint64_t>::max();
}
bool native_move_terrain(native_service::Point from,native_service::Point to)const {
    return from!=to&&native_service::distance(from,to)==1&&!static_blocked({to.x,to.y});
}
bool native_bank_altar(native_service::Point p)const {
    for(const auto at:altars)if(p==native_service::Point{at.x,at.y})return true;
    return false;
}
bool native_at_home(native_service::Point p)const {
    for(const auto at:executable_home_cache())if(p==native_service::Point{at.x,at.y})return true;
    return false;
}
bool native_runtime_commit_known(std::uint64_t serial)const {return serial&&serial<=native_service_state().last_ack;}
void native_record_physical(const native_service::Receipt& receipt,std::uint64_t prefix){
    using namespace native_service;auto& s=native_service_state();
    if(!receipt.attempted)return;
    const auto& target=native_facts().camp;
    const EgressJob job{target.id==receipt.action.target_id?target.group_ordinal:s.group,
        target.id==receipt.action.target_id?target.member_ordinal:s.member,receipt.action.target_id};
    const EgressWork physical{receipt.action.kind,
        receipt.applied==Truth::yes?EgressEffect::yes:receipt.applied==Truth::no?EgressEffect::no:EgressEffect::unknown,
        receipt.serial,prefix,receipt.before_epoch,receipt.before_pose,job};
    const auto update=s.egress.observe_work(physical);
    if(update==EgressUpdate::created||update==EgressUpdate::extended)++s.work_debts;
    if(target_primitive(receipt.action.kind)&&job.group>=0&&job.member>=0){
        const auto group_id=std::uint32_t(handle_camp_group_t(groups[std::size_t(job.group)]));
        s.cycle.observe_work({physical,{group_id,receipt.action.target_id,job.group,job.member},native_potential(receipt.before_pose)>0});
    }
    // Known START belongs to this command owner even if channel readback is
    // delayed beyond its completion. This is history, never payout authority.
    if(receipt.action.kind==PrimitiveKind::bank&&receipt.applied==Truth::yes)
        s.bank_started_owner=s.command_owner;
    if(receipt.action.kind==PrimitiveKind::move&&s.route.active()&&s.command_owner==s.route.goal().token){
        const RouteMove move{s.command_owner,receipt.serial,receipt.before_epoch,receipt.after_epoch,
            receipt.before_pose,receipt.action.point,receipt.applied==Truth::yes};
        if(receipt.applied==Truth::yes)s.route.move_applied(move);
        else if(receipt.applied==Truth::unknown)s.route.move_unresolved(move);
    }
    if(receipt.action.kind==PrimitiveKind::move&&receipt.applied==Truth::yes)native_advance_exit_witness(receipt);
}
void native_move_validation(unsigned reason,native_service::Point point,const native_service::SelfFact& me){
    using namespace native_service;auto& s=native_service_state();s.move_reason=reason;s.validation_point=point;
    native_trace_move_reason(reason,point);
    if(reason>=1&&reason<=4&&s.route.active()){
        const auto kind=reason==1?RouteRejectionKind::terrain:reason==2?RouteRejectionKind::camp:
            reason==3?RouteRejectionKind::champion:RouteRejectionKind::teleport;
        s.route.reject({s.route.goal().token,++s.rejection_serial,native_facts().epoch,me.pose,point,kind,false});
    }else if(reason>=5&&s.route.active())s.route.release_exclusion(me.pose,point,native_facts().epoch);
    if(reason==2&&s.blocker_round!=epoch){
        s.blocker_round=epoch;s.blocker_member=-1;
        // The fresh SDK camp refusal already recorded the dated obstruction.
        // Optional identity enrichment cannot scan the catalogue before the
        // next paid physical class. Unsupported index leaves identity Unknown.
        if(s.stationary_valid){const auto ordinal=s.stationary.at(point);
            if(ordinal>=0&&std::size_t(ordinal)<members.size()&&
               point==Point{members[std::size_t(ordinal)].pos.x,members[std::size_t(ordinal)].pos.y})s.blocker_member=ordinal;}
    }
    if(reason==6)native_note_camp_body(point,false);
}
bool native_runtime_commit(const native_service::Receipt& r) {
    using namespace native_service;auto& s=native_service_state();if(native_runtime_commit_known(r.serial))return true;
    if(!ack_service(s.service,r.serial,r.action.kind,r.applied==Truth::yes?ServiceApplied::yes:r.applied==Truth::no?ServiceApplied::no:ServiceApplied::unknown))return false;
    s.last_ack=r.serial;
    if(s.route.feedback_pending()&&r.action.kind==PrimitiveKind::move){
        const auto& me=native_facts().self;s.route.close_feedback({r.serial,true},me,native_programme_frame(me));
    }
    if(r.applied==Truth::yes){last_action_round=epoch;
        if(s.route.active()&&s.command_owner==s.route.goal().token){
            if(target_primitive(r.action.kind)&&native_facts().camp.id==r.action.target_id&&native_facts().camp.group_id==s.route.goal().group)
                s.route.service_actual({s.command_owner,++s.service_event,r.after_epoch,RouteServiceKind::work_effect,true,true});
            else if(r.action.kind==PrimitiveKind::bank&&r.after_actual&&r.submitting_after){
                s.route.service_actual({s.command_owner,++s.service_event,r.after_epoch,RouteServiceKind::bank_start,true,true});
                s.bank_started_owner=s.command_owner;
            }
        }
        switch(r.action.kind){
        case PrimitiveKind::basic:++s.work_commands;break;
        case PrimitiveKind::cast_unit:case PrimitiveKind::cast_ground:++s.work_commands;++s.cast_commands;break;
        case PrimitiveKind::move:++s.move_commands;s.excluded_count=0;native_note_camp_body(r.action.point,false);break;
        case PrimitiveKind::bank:++s.bank_starts;break;
        case PrimitiveKind::recall:++s.recall_starts;s.recovering=true;break;
        default:++s.cast_commands;break;
        }
    }else if(r.refused){++s.refused;
        if(r.action.kind==PrimitiveKind::move&&s.route.active())
            s.route.reject({s.route.goal().token,++s.rejection_serial,r.after_epoch,r.before_pose,r.action.point,RouteRejectionKind::unknown,true});
    }
    return true;
}
void native_route_changed(std::size_t index){
    auto& s=native_service_state();
    s.frontier.retire();++s.board_generation;
    if(s.exit_witness.prices_complete()){
        const native_service::Point changed{int(index%walls->width()),int(index/walls->width())};
        if(!native_service::retained_exit_survives_camp_edit(s.exit_witness,changed,s.camp_routes[index].excluded())){
            s.exit_witness={};s.exit_witness_retained=false;}
        else s.exit_witness_retained=true;
    }
    if constexpr(cfg::trace>=12)++native_service_trace().overlay_changes;
    // These dated exclusions nominate routes only. They never grant/deny an
    // SDK command. Retire the borrowed field BEFORE its board may be edited.
    s.fields.retire_all();s.nav_intent=0;s.approach_cursor=0;s.approach_prepared=false;s.root_count=0;s.bank_roots_materialized=false;
    if(s.board_edit_count<s.board_edits.size())s.board_edits[s.board_edit_count++]=index;
    else{s.board_ready=false;s.board_cursor=0;s.board_edit_count=0;}
}
void native_note_camp_body(native_service::Point point,bool blocked,native_service::Epoch acquired={}){
    auto& s=native_service_state();if(static_blocked({point.x,point.y}))return;
    const auto index=std::size_t(point.y)*walls->width()+std::size_t(point.x);
    if(index>=s.camp_routes.size())return;
    if(acquired.round<0)acquired=native_facts().epoch;
    auto& cell=s.camp_routes[index];const auto before=cell.excluded();
    const auto result=cell.observe(acquired,blocked);
    if(result==native_service::RouteUpdate::conflict||result==native_service::RouteUpdate::invalid)++s.unsupported;
    if(before==cell.excluded())return;
    if(cell.excluded())s.camp_mask_requested=true;
    native_route_changed(index);
}
void native_note_group_lifecycle(const native_service::GroupFact& group){
    auto& s=native_service_state();if(!group.current(native_facts().epoch)||group.ordinal<0||std::size_t(group.ordinal)>=s.route_group_life.size())return;
    const auto g=std::size_t(group.ordinal);const auto life=unsigned(group.life);
    if(s.route_group_life[g]==life)return;
    if constexpr(cfg::trace>=12)++native_service_trace().lifecycle_changes;
    s.route_group_life[g]=life;s.route_group_transition[g]=group.epoch;
    s.route_clear_cursor[g]=memory[g].begin;s.route_clear_pending[g]=true;
    s.frontier.retire();++s.board_generation;
    // No board byte changes here. Later publication checks each actual edit
    // against the small stored path before writing the retired shared board.
    if(s.exit_witness.prices_complete())s.exit_witness_retained=true;
    s.fields.retire_all();s.nav_intent=0;s.approach_cursor=0;s.approach_prepared=false;s.root_count=0;s.bank_roots_materialized=false;
}
void native_prepare_lifecycle_overlay(){
    auto& s=native_service_state();unsigned count=0;
    for(std::size_t g=0;g<groups.size()&&g<s.route_clear_pending.size()&&count<16;++g){
        if(!s.route_clear_pending[g])continue;
        auto& cursor=s.route_clear_cursor[g];
        while(cursor<memory[g].end&&count<16){const auto at=members[cursor++].pos;++count;
            const auto index=std::size_t(at.y)*walls->width()+std::size_t(at.x);
            auto& cell=s.camp_routes[index];const auto before=cell.excluded();
            cell.invalidate_before(s.route_group_transition[g]);
            if(before!=cell.excluded())native_route_changed(index);
        }
        if(cursor==memory[g].end)s.route_clear_pending[g]=false;
    }
}
void native_apply_board_edits(){
    auto& s=native_service_state();
    for(unsigned i=0;i<s.board_edit_count;++i){const auto at=s.board_edits[i];
        if(at<s.board_cursor)s.passable[at]=!s.camp_routes[at].excluded()&&
            (static_bits[at/8]&(1u<<(at%8)))==0;
    }s.board_edit_count=0;
}
bool native_acquire_camp_mask(native_service::Budget& budget){
    auto& s=native_service_state();
    if(!s.camp_mask_requested||(s.camp_mask_available&&s.camp_mask_cursor<members.size()))return false;
    const auto& self=native_facts().self;
    if(!self.actual(native_facts().epoch)||!self.alive||self.recalling||self.submitting)return false;
    const auto& layout=detail::layout();const auto bytes=bitboard::linear_buffer_size();
    if(layout.board_bytes>sizeof(s.camp_mask_raw)||bytes>s.camp_mask_linear.size()){
        s.camp_mask_requested=false;++s.unsupported;return false;
    }
    const auto acquired=native_facts().epoch;const auto outer=budget.held_tail;
    s.camp_mask_available=false;
    // Acquisition and ABI-supported packed publication are separate paid
    // imports. No engine-chunk padding/indexing formula is assumed.
    budget.reserve(native_service::Budget::add(outer,budget.query_bound));
    if(!budget.before_query()){budget.reserve(outer);return false;}
    const auto code=detail::sdk_camp_occupancy(s.camp_mask_raw.data());
    const bool paid=budget.after_query();budget.reserve(outer);detail::check(code);if(!paid)return false;
    if(!budget.before_query())return false;
    const auto exported=detail::sdk_Bitboard_ExportLinearBuffer(s.camp_mask_raw.data(),s.camp_mask_linear.data(),bytes);
    const bool publication_paid=budget.after_query();detail::check(exported);if(!publication_paid)return false;
    s.camp_mask_epoch=acquired;s.camp_mask_cursor=0;s.camp_mask_available=true;s.camp_mask_requested=false;return true;
}
void native_publish_camp_mask(){
    auto& s=native_service_state();if(!s.camp_mask_available)return;
    // The immutable snapshot retains its ACQUISITION epoch across inputs.
    // RouteCell rejects old positives after newer empty/lifecycle evidence.
    for(unsigned count=0;count<16&&s.camp_mask_cursor<members.size();++count){
        const auto at=members[s.camp_mask_cursor++].pos;
        const auto index=std::size_t(at.y)*walls->width()+std::size_t(at.x);
        if(s.camp_mask_linear[index/8]&(1u<<(index%8)))native_note_camp_body({at.x,at.y},true,s.camp_mask_epoch);
    }
    if(s.camp_mask_cursor==members.size())s.camp_mask_available=false;
}
bool native_runtime_alternative_move(const native_service::Primitive& previous,unsigned,native_service::Primitive& out) {
    auto& s=native_service_state();if(s.excluded_count<s.excluded.size())s.excluded[s.excluded_count++]=previous.point;
    for(unsigned i=0;i<s.move_count;++i){bool blocked=false;
        for(unsigned k=0;k<s.excluded_count;++k)blocked|=s.moves[i]==s.excluded[k];
        if(!blocked){out={native_service::PrimitiveKind::move,0,s.moves[i],0};return true;}
    }return false;
}
bool native_prepare_board(native_service::Budget& budget) {
    auto& s=native_service_state();if(s.board_ready)return true;
    const auto cells=std::size_t(walls->width())*walls->height();
    if(cells>s.passable.size()){++s.unsupported;return false;}
    for(unsigned n=0;n<128&&s.board_cursor<cells;++n,++s.board_cursor)
        s.passable[s.board_cursor]=!s.camp_routes[s.board_cursor].excluded()&&
            (static_bits[s.board_cursor/8]&(1u<<(s.board_cursor%8)))==0;
    s.prepare_units+=128;if(!budget.checkpoint())return false;
    return s.board_ready=s.board_cursor==cells;
}
void native_add_root(native_service::Point p) {
    auto& s=native_service_state();if(static_blocked({p.x,p.y}))return;
    for(unsigned i=0;i<s.root_count;++i)if(s.roots[i]==p)return;
    if(s.root_count<s.roots.size())s.roots[s.root_count++]=p;
}
void native_region(native_service::Point p,int radius) {
    for(int y=std::max(0,p.y-radius);y<=std::min(int(walls->height())-1,p.y+radius);++y)
        for(int x=std::max(0,p.x-radius);x<=std::min(int(walls->width())-1,p.x+radius);++x)native_add_root({x,y});
}
std::uint64_t native_potential(native_service::Point p)const {
    if(static_blocked({p.x,p.y}))return std::numeric_limits<std::uint64_t>::max();
    // Static potential is a nomination price, never actual life/strength/safety.
    return support[std::size_t(p.y)*walls->width()+std::size_t(p.x)];
}
int native_choose_group(const native_service::SelfFact& me) {
    auto& s=native_service_state();int best=-1;double best_time=std::numeric_limits<double>::infinity();
    for(std::size_t g=0;g<groups.size()&&g<s.unavailable_until.size();++g){
        if(epoch<s.unavailable_until[g]||memory[g].begin==memory[g].end)continue;
        int travel=10000,to_bank=10000;double hp=0;
        for(auto m=memory[g].begin;m<memory[g].end;++m){const auto at=members[m].pos;
            travel=std::min(travel,std::max(0,dist({me.pose.x,me.pose.y},at)-me.attack_range));hp+=members[m].base_hp;
            for(const auto altar:altars)to_bank=std::min(to_bank,std::max(0,dist(at,altar)-int(constants::submit_gems_range)));
        }
        // Declared nominal full-service time per TEAM reward; no owner share is
        // predicted. Current target facts are acquired separately.
        const auto time=(travel+hp/std::max(1.0,double(me.strength*camp_multiplier))+constants::capture_time+
            to_bank+constants::submit_gem_base_duration)/std::max(1u,groups[g].gem_value());
        if(time<best_time){best_time=time;best=int(g);}
    }
    if(best>=0)native_programme_job(best,int(memory[std::size_t(best)].begin));return best;
}
void native_goal(unsigned kind,const native_service::SelfFact& me,int range=0) {
    auto& s=native_service_state();s.group_goal=false;s.active_field=kind==4?native_service::ServiceField::egress:native_service::ServiceField::service;
    const auto key=kind==4?native_egress_key():(std::uint64_t(kind+1)<<56)^s.intent^
        (std::uint64_t(std::uint32_t(s.member+1))<<24)^(std::uint64_t(range)<<48);
    if(kind==4){
        s.excluded_count=0;s.goal_key=key;s.root_count=s.egress_root_count;
        for(unsigned i=0;i<s.root_count;++i)s.roots[i]=s.egress_roots[i];
        s.nav_intent=s.fields.semantic_intent(s.active_field)==key?key:0;
        s.field_revision=s.fields.generation(s.active_field);
        if(s.root_count)s.exit_anchor=s.roots[0];
        return;
    }
    if(s.goal_key!=key){s.excluded_count=0;s.goal_key=key;}
    if(s.nav_intent==key)return;s.root_count=0;s.bank_roots_materialized=false;
    if(kind==0&&s.member>=0)native_region({members[std::size_t(s.member)].pos.x,members[std::size_t(s.member)].pos.y},range);
    else if(kind==1&&s.group>=0){int closest=10000;native_service::Point chosen{};
        for(auto i=memory[std::size_t(s.group)].begin;i<memory[std::size_t(s.group)].end;++i){
            const native_service::Point candidate{members[i].pos.x,members[i].pos.y};
            const auto d=native_service::distance(me.pose,candidate);
            if(d<closest){closest=d;chosen=candidate;}
        }if(closest<10000)native_add_root(chosen);
    }
    else if(kind==2){int closest=10000,index=-1;
        for(std::size_t i=0;i<altars.size();++i){const int d=dist({me.pose.x,me.pose.y},altars[i]);if(d<closest){closest=d;index=int(i);}}
        s.bank_altar=index;if(index>=0)native_programme_bank_goal({altars[std::size_t(index)].x,altars[std::size_t(index)].y},index);
    }else if(kind==3){for(const auto p:executable_home_cache())native_add_root({p.x,p.y});}
    // The byte board is immutable while Navigation borrows it. Until its
    // staged fill is complete, retain the roots for direct adjacent TRY
    // nominations; preparing a field never suppresses an actual Basic.
    // Binding/copying and field progress belong to the post-incumbent tail.
}
void native_prepare_exit_root(){
    auto& s=native_service_state();if((s.goal_key>>56)!=5||s.root_count)return;
    // A retained square-ring cursor visits at most16 new cells AFTER dispatch.
    // No repeated interiors or root deduplication precede an exit primitive.
    for(unsigned n=0;n<16&&s.exit_radius<=unsigned(std::max(walls->width(),walls->height()));++n){
        const int r=int(s.exit_radius),side=int(s.exit_offset/(2*s.exit_radius)),at=int(s.exit_offset%(2*s.exit_radius));
        native_service::Point p=s.exit_anchor;
        if(side==0){p.x+=-r+at;p.y-=r;}else if(side==1){p.x+=r;p.y+=-r+at;}
        else if(side==2){p.x+=r-at;p.y+=r;}else{p.x-=r;p.y+=r-at;}
        if(++s.exit_offset==8*s.exit_radius){++s.exit_radius;s.exit_offset=0;}
        if(native_potential(p)==0){s.roots[0]=p;s.root_count=1;s.nav_intent=0;return;}
    }
}
void native_prepare_approach_roots(){
    auto& s=native_service_state();if((s.goal_key>>63)!=1||s.approach_prepared)return;
    const int left=std::max(0,s.approach_goal.x-s.approach_range),right=std::min(int(walls->width())-1,s.approach_goal.x+s.approach_range);
    const int top=std::max(0,s.approach_goal.y-s.approach_range),bottom=std::min(int(walls->height())-1,s.approach_goal.y+s.approach_range);
    const unsigned width=unsigned(right-left+1),total=width*unsigned(bottom-top+1);
    for(unsigned n=0;n<16&&s.approach_cursor<total;++n,++s.approach_cursor){
        const native_service::Point p{left+int(s.approach_cursor%width),top+int(s.approach_cursor/width)};
        // The living centre is not a walking perch. Full queried Basic range
        // is retained for ranged actors. Visibility remains a fresh dependency.
        if(p!=s.approach_goal&&!static_blocked({p.x,p.y})&&s.root_count<s.roots.size())s.roots[s.root_count++]=p;
    }s.approach_prepared=s.approach_cursor==total;
}
unsigned native_nominate_moves(native_service::Budget&,const native_service::SelfFact& me,bool exit=false) {
    auto& s=native_service_state();s.move_count=0;if(!me.can_move(native_facts().epoch))return 0;
    if(s.board_ready&&s.nav_intent&&s.nav_intent==s.goal_key){
        s.move_count=native_navigation().nominees(me.pose,{s.excluded.data(),s.excluded_count},s.moves);
        if(s.move_count)return s.move_count;
    }
    struct Choice{native_service::Point p;std::uint64_t price;int distance;};std::array<Choice,8> options{};unsigned count=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;
        const native_service::Point to{me.pose.x+dx,me.pose.y+dy};if(!native_move_terrain(me.pose,to))continue;
        bool excluded=false;for(unsigned k=0;k<s.excluded_count;++k)excluded|=s.excluded[k]==to;if(excluded)continue;
        int nearest=10000;for(unsigned k=0;k<s.root_count;++k)nearest=std::min(nearest,native_service::distance(to,s.roots[k]));
        options[count++]={to,native_potential(to),nearest};
    }
    std::stable_sort(options.begin(),options.begin()+count,[&](const Choice& a,const Choice& b){
        if(exit&&a.price!=b.price)return a.price<b.price;
        if(a.distance!=b.distance)return a.distance<b.distance;
        if(a.price!=b.price)return a.price<b.price;
        return a.p.y!=b.p.y?a.p.y<b.p.y:a.p.x<b.p.x;});
    for(unsigned i=0;i<count;++i)s.moves[s.move_count++]=options[i].p;return s.move_count;
}
bool native_dispatch(native_service::Budget& budget,native_service::Word word) {
    auto& s=native_service_state();word.epoch=native_facts().epoch;
    const auto& me=native_facts().self;
    if(s.route.active()&&me.actual(native_facts().epoch)){
        if(s.route.latest_epoch()!=me.epoch){s.route.begin(me);s.route.observe(me,native_programme_frame(me));}
        const auto cost=native_service::word_tail(word,0,budget,0,true),outer=budget.held_tail;
        if(!budget.admit(native_service::Budget::add(outer,cost)))return false;
        budget.reserve(native_service::Budget::add(outer,cost));
        const native_service::RouteFunding funding{s.route.goal().token,++s.funding_serial,native_facts().epoch,
            s.route.need(),true,true,s.route.goal().token};
        const bool funded=s.route.fund(funding);budget.reserve(outer);if(!funded)return false;
    }
    s.command_owner=s.route.active()?s.route.goal().token:0;
    const auto out=native_execute_word(budget,s.execution,word);s.last_status=unsigned(out.status);
    if(out.attempted)++s.programmes;return out.attempted||s.execution.pending;
}
bool native_walk_goal(native_service::Budget& budget,const native_service::SelfFact& me,bool exit=false) {
    auto& s=native_service_state();if(!native_nominate_moves(budget,me,exit))return false;
    for(unsigned i=0;i<std::min(s.move_count,3u)&&!budget.poisoned;++i){const auto to=s.moves[i];
        if(native_dispatch(budget,native_service::single_primitive({native_service::PrimitiveKind::move,0,to,0})))return true;
        if(s.execution.input_blocked||s.execution.pending)return false;
        if(s.excluded_count<s.excluded.size())s.excluded[s.excluded_count++]=to;
    }return false;
}
bool native_direct_approach(native_service::Budget& budget,const native_service::SelfFact& me,native_service::Point goal) {
    auto& s=native_service_state();s.move_count=0;
    if(!me.can_move(native_facts().epoch))return false;
    const auto key=(std::uint64_t(1)<<63)^s.intent^(std::uint64_t(std::uint32_t(s.member+1))<<24);
    // A member key alone cannot retain a different destination. Corpse probes
    // can change the member after a previous local spawn was nominated.
    if(s.goal_key!=key||s.approach_goal!=goal||s.approach_range!=me.attack_range){
        if constexpr(cfg::trace>=12)++native_service_trace().approach_changes;
        s.goal_key=key;s.approach_goal=goal;s.approach_range=std::max(1,me.attack_range);
        s.approach_cursor=0;s.approach_prepared=false;s.root_count=0;s.excluded_count=0;s.nav_intent=0;
    }
    if(s.nav_intent==key&&s.board_ready)
        s.move_count=native_navigation().nominees(me.pose,{s.excluded.data(),s.excluded_count},s.moves);
    if(!s.move_count){
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;
            const native_service::Point to{me.pose.x+dx,me.pose.y+dy};if(!native_move_terrain(me.pose,to))continue;
            bool excluded=false;for(unsigned k=0;k<s.excluded_count;++k)excluded|=s.excluded[k]==to;
            if(!excluded)s.moves[s.move_count++]=to;
        }
        // Fixed eight-element insertion sort; no roots, SDK masks or fields
        // are constructed in front of the first executable nomination.
        for(unsigned i=1;i<s.move_count;++i){const auto value=s.moves[i];unsigned j=i;
            while(j){const auto previous=s.moves[j-1];
                const auto vd=native_service::distance(value,goal),pd=native_service::distance(previous,goal);
                if(vd>pd||(vd==pd&&native_potential(value)>=native_potential(previous)))break;
                s.moves[j]=previous;--j;
            }s.moves[j]=value;
        }
    }
    if(!s.move_count){s.excluded_count=0;return false;}
    for(unsigned i=0;i<std::min(s.move_count,3u)&&!budget.poisoned;++i){const auto to=s.moves[i];
        if(native_dispatch(budget,native_service::single_primitive({native_service::PrimitiveKind::move,0,to,0})))return true;
        if(s.execution.input_blocked||s.execution.pending)return false;
        if(s.excluded_count<s.excluded.size())s.excluded[s.excluded_count++]=to;
    }return false;
}
#include "native_service_group_runtime.h"
#include "native_service_finite_work.h"
#include "native_service_programme.h"
#include "native_service_recovery_geometry.h"
void native_service_step(std::uint64_t whole_entry) {
    using namespace native_service;Budget budget{this,native_clock,whole_entry};
    budget.pure_bound=20000;NativeServiceTraceGuard trace_guard{budget};native_service_body(budget);
    auto& s=native_service_state();
    if(s.execution.pending||!budget.checkpoint())return;
    if(budget.pure_used()+3000<=budget.pure_bound){native_acquire_camp_mask(budget);if(budget.poisoned)return;}
    if(budget.pure_used()+1500<=budget.pure_bound){native_publish_camp_mask();native_apply_board_edits();}
    if(budget.pure_used()+3000<=budget.pure_bound){
        native_prepare_lifecycle_overlay();native_apply_board_edits();
        if(!s.group_goal)native_prepare_approach_roots();native_prepare_board(budget);
    }
    if(budget.poisoned||!s.board_ready||s.board_edit_count)return;
    // Egress is maintained BEFORE it is needed by a hazardous Work prefix.
    // Purpose selection changes a reference, never the other field's progress.
    native_prepare_egress_field(budget);if(budget.poisoned)return;
    if(s.active_field==ServiceField::egress||!s.goal_key)return;
    if(s.group_goal){native_prepare_group_frontier(budget);return;}
    if((s.goal_key>>63)==1&&!s.approach_prepared)return;
    if(s.fields.semantic_intent(ServiceField::service)!=s.goal_key){
        if(!s.root_count||!budget.checkpoint()||budget.pure_used()+3000>budget.pure_bound)return;
        if(!s.fields.begin(ServiceField::service,walls->width(),walls->height(),{s.passable.data(),s.board_cursor},
           {s.roots.data(),s.root_count},s.goal_key))return;
    }
    s.nav_intent=s.goal_key;s.field_revision=s.fields.generation(ServiceField::service);
    if(!budget.checkpoint()||budget.pure_used()+600>budget.pure_bound)return;
    const auto units=budget.pure_used()+5000<=budget.pure_bound?32u:1u;
    const auto progress=s.fields.step_guarded(ServiceField::service,units,[&]{
        return budget.checkpoint()&&budget.pure_used()+3000<=budget.pure_bound;
    });s.prepare_units+=progress.units;budget.checkpoint();
}
void native_late_diagnostic(){if constexpr(cfg::trace<0){
    // This one-shot packet runs AFTER the complete ordinary body. All normal
    // trace counters remain OFF. Its first input needs a public-prefix join.
    if(epoch!=-cfg::trace||!fuel(20000))return;
    const auto& s=native_service_state();const auto& me=native_facts().self;
    std::array<std::uint32_t,112> packet{};
    packet[0]=0x324c4e4du;packet[1]=(2u<<16)|std::uint32_t(packet.size()*sizeof(packet[0]));
    packet[2]=epoch;packet[3]=std::uint32_t(handle_champion_t(self()));
    packet[4]=s.stage;packet[5]=s.last_status;packet[6]=s.group;packet[7]=s.member;
    packet[8]=unsigned(s.service.phase);packet[9]=unsigned(s.service.resume_phase);packet[10]=s.service.bank_obligation;
    packet[11]=s.service.bag_actual;packet[12]=s.service.bag;packet[13]=s.recovering;packet[14]=s.exit_due;
    packet[15]=s.last_gems;packet[16]=me.gems;packet[17]=me.gems_actual;packet[18]=me.pose.x;packet[19]=me.pose.y;
    std::memcpy(&packet[20],&me.hp,sizeof(me.hp));std::memcpy(&packet[21],&me.max_hp,sizeof(me.max_hp));
    std::memcpy(&packet[22],&me.strength,sizeof(me.strength));packet[23]=me.actual(native_facts().epoch);
    packet[24]=me.raw_m;packet[25]=me.raw_a;packet[26]=me.raw_c;
    packet[27]=unsigned(me.move_restricted)|unsigned(me.attack_restricted)<<1|unsigned(me.cast_restricted)<<2;
    packet[28]=me.alive;packet[29]=me.recalling;packet[30]=me.submitting;packet[31]=native_potential(me.pose);
    packet[32]=s.bank_altar;packet[33]=packet[34]=0xffffffffu;
    if(s.bank_altar>=0&&std::size_t(s.bank_altar)<altars.size()){
        packet[33]=altars[std::size_t(s.bank_altar)].x;packet[34]=altars[std::size_t(s.bank_altar)].y;}
    packet[35]=constants::submit_gems_range;packet[36]=s.goal_key;packet[37]=s.goal_key>>32;
    packet[38]=s.nav_intent;packet[39]=s.nav_intent>>32;packet[40]=unsigned(native_navigation().status(me.pose));
    packet[41]=unsigned(native_navigation().status());packet[42]=native_navigation().distance(me.pose).value_or(-1);
    packet[43]=s.root_count;packet[44]=s.root_count?s.roots[0].x:0xffffffffu;packet[45]=s.root_count?s.roots[0].y:0xffffffffu;
    packet[46]=s.approach_goal.x;packet[47]=s.approach_goal.y;packet[48]=s.approach_range;packet[49]=s.approach_prepared;
    packet[50]=s.field_revision;packet[51]=s.prepare_units;packet[52]=s.camp_mask_available;
    packet[53]=s.camp_mask_cursor;packet[54]=s.camp_mask_requested;packet[55]=s.camp_mask_epoch.round;
    packet[56]=s.execution.pending;packet[57]=s.execution.input_blocked;packet[58]=unsigned(s.execution.next);
    packet[59]=s.execution.input_attempts;packet[60]=s.execution.input_successes;packet[61]=s.execution.input_refusals;
    packet[62]=s.last_ack;packet[63]=s.service.accepted_prefix;
    const auto& trace=native_service_trace();
    packet[64]=trace.pure;packet[65]=trace.poisoned;packet[66]=trace.queries;packet[67]=trace.commands;
    packet[68]=s.fields.generation(native_service::ServiceField::service);
    packet[69]=unsigned(s.fields.status(native_service::ServiceField::service));
    packet[70]=unsigned(s.fields.status(native_service::ServiceField::service,me.pose));
    packet[71]=s.fields.distance(native_service::ServiceField::service,me.pose).value_or(-1);
    packet[72]=s.fields.generation(native_service::ServiceField::egress);
    packet[73]=unsigned(s.fields.status(native_service::ServiceField::egress));
    packet[74]=unsigned(s.fields.status(native_service::ServiceField::egress,me.pose));
    packet[75]=s.fields.distance(native_service::ServiceField::egress,me.pose).value_or(-1);
    packet[76]=s.exit_witness_retained;packet[77]=unsigned(s.exit_witness.status);packet[78]=s.exit_witness.steps;
    packet[79]=s.exit_witness.ranged_with_refusal;packet[80]=s.exit_witness.tag.origin.x;packet[81]=s.exit_witness.tag.origin.y;
    packet[82]=s.exit_witness.tag.field_generation;packet[83]=unsigned(s.cycle.phase());
    packet[84]=s.cycle.restore_required();packet[85]=s.cycle.has_egressed();packet[86]=s.cycle.batch_active();
    packet[87]=s.cycle.retained_job().group_ordinal;packet[88]=s.cycle.retained_job().member_ordinal;
    packet[89]=s.cycle.conditional();packet[90]=s.cycle.ownership_conflict();packet[91]=s.egress.outstanding();
    packet[92]=s.egress.direct_required();packet[93]=s.egress.replacement_owner();packet[94]=s.egress.latest().serial;
    packet[95]=s.egress.latest().accepted_prefix;packet[96]=s.rank_decreases;packet[97]=s.work_debts;
    packet[98]=s.egress_closures;packet[99]=s.coverage_transfers;packet[100]=s.restorations;
    packet[101]=s.batch_next_input_exit;packet[102]=s.capture_group;packet[103]=s.capture_deadline;
    packet[104]=s.capture_reobserve;packet[105]=unsigned(s.active_field);packet[106]=trace.last;
    packet[107]=trace.sdk_spent;packet[108]=s.egress_root_count;
    packet[109]=s.fields.available(native_service::ServiceField::egress,s.exit_witness.tag.origin);
    packet[110]=s.exit_witness.prices_complete();packet[111]=native_service_field_matches();
    emit_packet(packet);
}}
