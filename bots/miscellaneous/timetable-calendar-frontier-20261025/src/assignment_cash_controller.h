#pragma once
// Included inside the controller's private namespace, after the ordinary Trail
// and guarded-step adapters. Immutable fields only nominate; every SDK action
// below refreshes current body/contact/epoch support.
namespace scash=assignment_cash_service;

[[maybe_unused]] scash::Point cash_point(vec2 p){return {p.x,p.y};}
[[maybe_unused]] vec2 cash_point(scash::Point p){return {p.x,p.y};}
[[maybe_unused]] scr::Goal cash_goal(scash::Goal g)
{return g==scash::Goal::Bank?scr::Goal::Bank:g==scash::Goal::Home?scr::Goal::Home:scr::Goal::Perch;}
[[maybe_unused]] std::uint32_t cash_duration(std::uint32_t gems)
{
    if(!gems)return 0;
    const double value=std::ceil(static_cast<double>(gems)*cambc::constants::submit_gem_duration_multiplier)+
        cambc::constants::submit_gem_base_duration;
    if(!std::isfinite(value)||value<0||value>std::numeric_limits<std::uint32_t>::max())return 0;
    return std::max(static_cast<std::uint32_t>(std::max(0,cambc::constants::submit_gem_minimum_duration)),
        static_cast<std::uint32_t>(value));
}
struct CashBackend
{
    Controller& state;const scr::Routes& routes;
    bool fresh(const scash::Frame& f) const {
        return budget(6000)&&cambc::self().position()==cash_point(f.pos)&&current(f.epoch);
    }
    bool clear_contact(const scash::Frame& f) const {
        if(!fresh(f))return false;
        const bool clear=cambc::vision::champion_tiles(cambc::team::enemy).is_empty();
        return clear&&fresh(f);
    }
    scash::Command moved(const scash::Frame& f,const PotentialAuthority& result,scr::Goal goal) {
        if(result.moved) {
            if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::command_receipt(
                assignment_reachability_probe::role(),result.epoch,2,static_cast<unsigned>(scash::CommandStatus::Applied));
            state.expected=result.pos;state.escape_required=false;
            const auto retained=record_applied_ordinary_step(state,{f.pos.x,f.pos.y},
                {result.pos.x,result.pos.y},immutable_camp_quiet(cash_point(f.pos)),immutable_camp_quiet(result.pos));
            const auto remaining=routes.distance(goal,{result.pos.x,result.pos.y});
            return {scash::CommandStatus::Applied,true,cash_point(result.pos),result.epoch,
                remaining?static_cast<int>(*remaining):-1,
                retained==production_walk::Status::Known&&fresh_after(result.pos,f.epoch),immutable_camp_quiet(result.pos),false};
        }
        if(result.status==Status::Stale) {
            const auto after=cambc::self().position();const int epoch=static_cast<int>(cambc::round_number());
            if(after!=cash_point(f.pos)&&state.walk) {
                state.walk->invalidate();
                if(immutable_camp_quiet(after))state.walk->anchor_quiet({after.x,after.y});
            }
            return {scash::CommandStatus::Stale,true,cash_point(after),epoch,-1,false,
                immutable_camp_quiet(after),false,true};
        }
        if(result.status==Status::NeedCompute)return {scash::CommandStatus::NeedCompute};
        if(result.status==Status::Ready&&!result.can_move)return {scash::CommandStatus::Spent};
        // Any failed point command invalidates the previous negative contact
        // premise. The pure owner requires a later completed observation.
        return {scash::CommandStatus::NeedContact};
    }
    bool fresh_after(vec2 p,int epoch) const
    {return budget(0)&&cambc::self().position()==p&&current(epoch);}
    scash::Command advance(const scash::Frame& f,scash::Goal requested,scash::Point destination) {
        const auto goal=cash_goal(requested);const auto& goals=routes.goals();
        const auto expected=goal==scr::Goal::Bank?goals.bank:goal==scr::Goal::Home?goals.home:goals.perch;
        if(expected!=scr::Point{destination.x,destination.y}||!clear_contact(f))return {scash::CommandStatus::NeedContact};
        const auto quote=routes.price(goal,{f.pos.x,f.pos.y},goal==scr::Goal::Bank?state_cash_inputs():0);
        if(!quote.survives(cambc::self().health())||!quote.first_step||!fresh(f))return {scash::CommandStatus::Denied};
        const auto next=*quote.first_step;
        const auto escape=routes.price(scr::Goal::Home,next,0);const auto raw=routes.static_geometry()->raw_upper(next);
        if(!escape.complete()||!raw)return {scash::CommandStatus::NoSupport};
        const double required=std::max(quote.raw_required,*raw+escape.raw_required);
        const auto result=transit_authority(required,cash_point(f.pos),{next.x,next.y});
        return moved(f,result,goal);
    }
    std::uint32_t state_cash_inputs() const {
        const auto& actor=cambc::self();
        if(actor.is_submitting_gems())return static_cast<std::uint32_t>(std::max(0,actor.remaining_channel_time()));
        return cash_duration(actor.gem_count());
    }
    scash::Command start_submit(const scash::Frame& f,scash::Point altar) {
        if(!clear_contact(f)||!f.channel_funded)return {scash::CommandStatus::NeedContact};
        const auto quote=routes.price(scr::Goal::Bank,{f.pos.x,f.pos.y},state_cash_inputs());
        if(!quote.survives(cambc::self().health())||!fresh(f))return {scash::CommandStatus::Denied};
        try {cambc::self().submit_gems(cash_point(altar));}
        catch(const cambc::error& e) {
            if(e.code()!=cambc::error_code::invalid_action_submit_gems_cant_channel&&
               e.code()!=cambc::error_code::invalid_action_submit_gems_no_gems&&
               e.code()!=cambc::error_code::invalid_action_submit_gems_bad_altar&&
               e.code()!=cambc::error_code::invalid_action_submit_gems_out_of_range&&
               e.code()!=cambc::error_code::invalid_action_submit_gems_already_used&&
               e.code()!=cambc::error_code::invalid_action_submit_gems_altar_locked)throw;
            return {scash::CommandStatus::Refused,true,cash_point(cambc::self().position()),
                static_cast<int>(cambc::round_number())};
        }
        const auto after=cambc::self().position();const int epoch=static_cast<int>(cambc::round_number());
        const bool channel=cambc::self().is_submitting_gems();
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::command_receipt(
            assignment_reachability_probe::role(),epoch,3,static_cast<unsigned>(scash::CommandStatus::Applied));
        return {scash::CommandStatus::Applied,true,cash_point(after),epoch,0,
            after==cash_point(f.pos)&&fresh(f),immutable_camp_quiet(after),channel};
    }
    scash::Command cancel_submit(const scash::Frame& f) {
        if(!fresh(f)||!cambc::self().is_submitting_gems())return {scash::CommandStatus::Stale};
        cambc::self().cancel_submit_gems();
        const auto after=cambc::self().position();const int epoch=static_cast<int>(cambc::round_number());
        return {scash::CommandStatus::Applied,true,cash_point(after),epoch,-1,
            after==cash_point(f.pos)&&fresh(f),immutable_camp_quiet(after),false};
    }
    scash::Command retreat(const scash::Frame& f) {
        if(!clear_contact(f))return {scash::CommandStatus::NeedContact};
        const auto quote=routes.price(scr::Goal::Home,{f.pos.x,f.pos.y},0);
        if(!quote.survives(cambc::self().health())||!quote.first_step||!fresh(f))return {scash::CommandStatus::NoSupport};
        const auto next=*quote.first_step;
        return moved(f,transit_authority(quote.raw_required,cash_point(f.pos),{next.x,next.y}),scr::Goal::Home);
    }
};

[[maybe_unused]] bool static_cash_input(Controller& state)
{
    auto& data=static_data(state);auto& owner=assignment_data(state);
    if(data.cash_contact_epoch>=0) {
        const int epoch=static_cast<int>(cambc::round_number());
        if(epoch<=data.cash_contact_epoch)return true; // No second physical attempt after refusal.
        if(!cambc::self().is_alive()){data.cash_contact_epoch=-1;return false;}
        auto contact=observe_service();if(contact.status!=Status::Ready)return true;
        data.cash_contact_epoch=-1;
        if(contact.enemies.empty())return false;
        Applied applied;service_threat(contact,applied);
        const auto actual=cambc::self().position();state.expected=actual;
        if(applied.moved&&state.walk) {
            state.walk->invalidate();
            if(immutable_camp_quiet(actual))state.walk->anchor_quiet({actual.x,actual.y});
        }
        state.escape_required=!immutable_camp_quiet(actual);invalidate(state);return true;
    }
    if(!data.active||!data.active_routes||!owner.choice||!data.active_programme)return false;
    if(!scash::active(data.cash)&&state.phase!=Phase::Bank)return false;
    if(!budget(30000))return true;
    const int epoch=static_cast<int>(cambc::round_number());const auto& actor=cambc::self();
    scash::Frame frame;frame.epoch=epoch;frame.programme=data.active_programme;
    const auto origin=actor.position();frame.pos=cash_point(origin);frame.hp=actor.health();frame.max_hp=actor.max_health();
    frame.gems=actor.gem_count();frame.gold=actor.gold_count();const auto flags=actor.state();
    frame.alive=flags.contains(cambc::champion_state::alive);frame.can_move=flags.contains(cambc::champion_state::can_move);
    frame.channeling=flags.contains(cambc::champion_state::submitting_gems);
    frame.other_channel=flags.contains(cambc::champion_state::recalling)||flags.contains(cambc::champion_state::teleporting);
    frame.can_submit=frame.alive&&!frame.channeling&&!frame.other_channel&&frame.gems>0;
    frame.own_spawn=World::get().own_spawn.is_set(origin);frame.quiet=immutable_camp_quiet(origin);
    frame.enemy=!cambc::vision::champion_tiles(cambc::team::enemy).is_empty();frame.enemy_known=true;
    frame.programme_applicable=owner.programme.no_shop()&&owner.validated.has_value();
    const auto inventory=actor.item_count();
    if(owner.validated) {
        frame.programme_applicable&=inventory.size()==owner.validated->inventory_size;
        std::size_t i=0;for(const auto count:inventory) {
            if(i>=owner.validated->inventory_size||count!=owner.validated->inventory[i])frame.programme_applicable=false;
            ++i;
        }
    }
    const auto inputs=frame.channeling?static_cast<std::uint32_t>(std::max(0,actor.remaining_channel_time())):cash_duration(frame.gems);
    frame.channel_remaining=frame.channeling?inputs:0;
    if(!data.cash_receipt_seen) {
        data.cash_receipt_seen=true;data.cash_renewal_not_before=data.cash_timing_epoch=-1;
    }
    if(data.cash_renewal_not_before<0)
        admission_lifecycle(state,owner.choice_local,epoch,origin);
    const auto redeploy=data.active_routes->distance(scr::Goal::Perch,data.active_routes->goals().home);
    frame.renewal_time_bound=data.cash_renewal_not_before>=0&&data.cash_timing_epoch>=0&&redeploy.has_value();
    frame.renewal_not_before=data.cash_renewal_not_before;frame.redeploy_inputs=redeploy?*redeploy:0;
    if(!scash::active(data.cash)) {
        const auto& goals=data.active_routes->goals();scash::Programme plan;
        plan.id=data.active_programme;plan.group=owner.choice_local;
        plan.bank_stand={goals.bank.x,goals.bank.y};plan.home={goals.home.x,goals.home.y};plan.perch={goals.perch.x,goals.perch.y};
        for(const auto altar:World::get().altars)
            if(distance({goals.bank.x,goals.bank.y},altar)<=cambc::constants::submit_gems_range){plan.altar=cash_point(altar);break;}
        plan.channel_inputs=inputs;
        const double healing=cambc::constants::spawn_healing;
        if(!std::isfinite(healing)||healing<=0||!inputs)return false;
        const double healing_inputs=std::ceil(1.0/healing);
        if(!std::isfinite(healing_inputs)||healing_inputs>std::numeric_limits<std::uint32_t>::max())return false;
        plan.heal_inputs=static_cast<std::uint32_t>(healing_inputs);
        if(actor.position()!=origin||!current(epoch))return true;
        frame.observation=++data.cash_observation;frame.read=scash::Read::Fresh;
        if(!scash::arm(data.cash,plan,frame))return false;
        data.cash_bank_total=cambc::global_submitted_gems(cambc::team::ally);
    }
    const auto mode=data.cash.mode;
    frame.route_goal=(mode==scash::Mode::ToHome||mode==scash::Mode::Heal||mode==scash::Mode::Returning)?
        scash::Goal::Home:(mode==scash::Mode::Redeploy||mode==scash::Mode::Reobserve)?scash::Goal::Perch:scash::Goal::Bank;
    const auto goal=cash_goal(frame.route_goal);const auto quote=data.active_routes->price(goal,{origin.x,origin.y},inputs);
    const auto escape=data.active_routes->price(scr::Goal::Home,{origin.x,origin.y},0);
    frame.return_funded=escape.survives(frame.hp);frame.wait_funded=frame.return_funded;
    frame.progress_funded=quote.survives(frame.hp);
    frame.channel_funded=data.active_routes->price(scr::Goal::Bank,{origin.x,origin.y},inputs).survives(frame.hp);
    const auto length=data.active_routes->distance(goal,{origin.x,origin.y});
    frame.route_ready=length.has_value();frame.distance=length?static_cast<int>(*length):-1;
    frame.route=ssw::nonzero(data.active_programme*4+static_cast<unsigned>(goal)+1);
    if(frame.gems==0&&!frame.channeling&&origin==cash_point(data.cash.plan.bank_stand)&&data.cash.submit_accepted)
        frame.bank_readback=cambc::global_submitted_gems(cambc::team::ally)>=data.cash_bank_total+data.cash.start_gems?
            scash::BankReadback::Confirmed:scash::BankReadback::EmptyObserved;
    if(frame.pos==data.cash.plan.perch) {
        const auto life=admission_lifecycle(state,data.cash.plan.group,epoch,origin);
        frame.life_read=life.status==Status::Ready?scash::Read::Fresh:scash::Read::Unavailable;
        frame.life=static_cast<scash::Life>(life.availability);
    }
    if(actor.position()!=origin||!current(epoch))return true;
    frame.observation=++data.cash_observation;frame.read=scash::Read::Fresh;
    CashBackend backend{state,*data.active_routes};const auto outcome=scash::dispatch(data.cash,frame,backend);
    if(scash::selected_home_heal(data.cash)) {
        owner.programme.handoff(assignment_protocol::ProgrammeHandoff::SelectedHomeHeal);
        state.phase=Phase::Recover;state.group=data.cash.plan.group;
    } else owner.programme.handoff(assignment_protocol::ProgrammeHandoff::SelectedContinuation);
    if(outcome==scash::Outcome::WorkReady||outcome==scash::Outcome::CaptureReady) {
        state.phase=outcome==scash::Outcome::WorkReady?Phase::Work:Phase::Capture;state.group=data.cash.plan.group;
        state.expected=actor.position();state.escape_required=false;state.close_front=false;state.partial_progress=false;
        if(state.phase==Phase::Capture)state.capture_bag=actor.gem_count();
        data.cash_receipt_seen=false;data.cash_renewal_not_before=data.cash_timing_epoch=-1;
        invalidate(state);return true;
    }
    if(outcome==scash::Outcome::Contact||outcome==scash::Outcome::Fallback||outcome==scash::Outcome::Revalue||
       outcome==scash::Outcome::NeedResolver) {
        const auto actual=actor.position();const bool physical_refutation=data.cash.physical_refutation;
        retire_assignment(state,physical_refutation);
        state.phase=actor.gem_count()?Phase::Bank:Phase::Acquire;state.group=-1;state.expected=actual;
        state.escape_required=!immutable_camp_quiet(actual);invalidate(state);
        if(outcome==scash::Outcome::Contact)data.cash_contact_epoch=epoch;
        return true;
    }
    return true;
}
