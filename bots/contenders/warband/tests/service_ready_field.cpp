#include "service_ready_field.h"
#include <cstdio>
#include <limits>
#include <type_traits>

using namespace wb::ready_field;
namespace {
int checks{}, failures{};
void check(bool ok, const char* message) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", message); }
}
Authority auth(int round, std::uint64_t revision = 1) { return {1, round, std::uint64_t(round), revision}; }
Episode episode() {
    Episode e; e.id = 5; e.sealed.owner = 1; e.sealed.group = 2;
    e.sealed.member_count = 1; e.sealed.member_ids[0] = 20;
    e.capture = {true, true, 2, 28, 20}; e.bank_serial_baseline = 10;
    e.opened_at = auth(120); e.terminal = Terminal::ReadyFieldHoldCash;
    e.deadline_round = 160; e.max_observation_calls = 32; return e;
}
wb::service::Frame frame(int round, std::uint64_t revision = 1) {
    wb::service::Frame f; f.authority = auth(round, revision); f.fresh = f.alive = true;
    f.position = {8,7}; f.hp = f.max_hp = 1200; f.attack_range = 1;
    f.can_move = f.can_attack = f.can_cast = true;
    f.domain_harmless = f.static_hazard_excluded = true;
    f.bag = 0; f.gold = 100; f.at_spawn = false;
    f.own_bank_receipt = {true, true, 28, 11}; return f;
}
Facts facts(int round, std::uint64_t revision = 1) {
    Facts o; o.authority = auth(round, revision);
    o.self_known = o.life_known = o.health_known = o.position_known = true;
    o.bag_known = o.gold_known = o.channel_known = o.stun_known = true;
    o.permissions_known = {true, true, true}; o.restrictions_known_mask = 63;
    o.cooldown_known = {true, true}; o.cooldown = {0,0};
    o.input_unused_known = o.input_unused = true;
    o.episode_continuity_known = o.same_episode = true;
    o.inventory.authority = o.authority; o.inventory.known = o.inventory.complete = true;
    o.inventory.count = 2; o.inventory.slots[0] = {InventoryKind::Item, 2, 1};
    o.inventory.slots[1] = {InventoryKind::Consumable, 1, 2};
    o.field = {o.authority, true, true, {8,7}}; return o;
}
NextBasicStart next_start(int round, PrimitiveSchema schema = full_ready(), std::uint64_t revision = 1) {
    NextBasicStart n; n.authority = auth(round, revision); n.schema = schema;
    n.scope = Scope::NPCOnlyNoFoes; n.known = n.scoped_assumptions_certified = true;
    n.profile_supported = n.whole_group_harmless = n.static_quiet_origin = n.static_quiet_landing = true;
    n.bounded_continuation_prepared = true; n.next_episode = 6;
    n.contract.owner = 1; n.contract.group = 3; n.contract.member_count = 1;
    n.contract.member_ids[0] = 30;
    auto& m = n.first; m.authority = n.authority;
    m.observed_self = m.observed_readiness = m.alive = true;
    m.position = {8,7}; m.hp = 1200; m.can_move = m.can_attack = m.can_cast = true;
    m.observed_domain = m.domain_supported = true;
    m.observed_target = m.target_visible = m.target_alive = m.basic_in_range = true;
    m.target_id = 30; m.target_position = {9,7}; m.target_hp = 120;
    n.primitive = {wb::service_micro::Kind::Basic, 30, {}}; return n;
}
OwnBankEvidence bank() {
    OwnBankEvidence b; b.authority = auth(121); b.owner = 1; b.group = 2;
    b.episode = 5; b.capture_serial = 20;
    b.source = ProofSource::ActorJoinedOwnCompletion; b.receipt = {true,true,28,11}; return b;
}
void latch(Endpoint& e, Episode goal = episode()) {
    check(e.seal(auth(120), goal), "seal explicit episode once");
    auto proof = bank();
    check(e.observe_prospective(auth(121),frame(121),facts(121),next_start(121),&proof).status == Status::BankVerified,
          "actor joined bank latches, never ready on receipt input");
}
void latch_observed(Endpoint& e, Episode goal = episode()) {
    check(e.seal(auth(120), goal), "observed episode seals once");
    auto proof = bank();
    check(e.observe_readiness(auth(121),frame(121),facts(121),bare_move_basic(),&proof).status
          == Status::BankVerified, "observed API latches actor-joined bank but never releases on bank input");
}
NominationEntry entry(int round, std::uint64_t revision = 1) {
    NominationEntry n; n.authority = auth(round,revision); n.position = {8,7};
    n.world_known = n.world_ready = n.position_known = true;
    n.static_quiet_known = n.static_npc_quiet = true;
    n.candidate_known = n.candidate_present = n.bag_known = true;
    n.input_unused_known = n.input_unused = true; return n;
}
void observed_endpoint_and_caller() {
    { Endpoint e; latch_observed(e); auto f=frame(122); auto o=facts(122);
      f.can_cast=false; o.permissions_known[2]=false;
      o.cooldown_known={false,false}; o.cooldown={8,13};
      o.restrictions_known_mask=3; o.restrictions=4;
      f.static_hazard_excluded=f.domain_harmless=false; f.capture_contested=true;
      o.field.static_npc_quiet=false; f.gold=837; o.inventory.slots[0].count=3;
      const auto r=e.observe_readiness(auth(122),f,o);
      check(r.status==Status::ObservedReadyForBasic && r.ticket_kind==TicketKind::Observed,
            "observed Bare endpoint needs no prepared next contract, target, route, C or cooldowns");
      check(r.field.known && !r.field.static_npc_quiet && r.field.position==f.position,
            "observed readiness retains actual nonquiet field value without safety claim");
      check(r.held.gold==837 && r.held.bag==0 && r.held.inventory.slots[0].count==3,
            "observed endpoint preserves literal held resources");
      auto n=entry(122); n.static_npc_quiet=false;
      check(!nomination_entry_allowed(r,auth(122),n),
            "true nonquiet body readiness does not admit Nominate");
      wb::service::Interpreter old; check(old.begin(episode().sealed),"prepare existing continuation");
      const auto phase=old.phase();
      check(e.begin_next(auth(122),r.ticket,old).reason==Reason::WrongTicketKind
            && old.phase()==phase, "observed ticket cannot invoke prospective begin or reset old Interpreter");
      const auto continued=e.continue_existing(auth(122),r.ticket);
      check(continued.reason==Reason::CallerContinues && continued.ticket==0
            && e.phase()==Phase::BankVerified && old.phase()==phase,
            "caller declines observed ticket while existing continuation remains untouched");
      check(continued.deadline_round==r.deadline_round
            && continued.observation_calls_left==r.observation_calls_left,
            "declining cannot reset deadline or finite observation allowance");
      check(e.observe_readiness(auth(122),f,o).reason==Reason::InputTicketConsumed,
            "same authority cannot remint declined ticket");
      f=frame(122,2);o=facts(122,2);
      check(e.observe_readiness(auth(122,2),f,o).reason==Reason::InputTicketConsumed,
            "new view in same physical input cannot remint declined ticket");
      const auto later=e.observe_readiness(auth(123),frame(123),facts(123));
      check(later.status==Status::ObservedReadyForBasic && later.ticket>r.ticket
            && later.observation_calls_left+3==r.observation_calls_left,
            "later fresh input offers unique ticket with retained depleted budget");
      check(nomination_entry_allowed(later,auth(123),entry(123)),
            "separate current quiet world/candidate/unused facts admit existing Nominate entry");
      check(e.release(auth(123),later.ticket).status==Status::Released && old.phase()==phase,
            "release acknowledges observed terminal without core Complete or Interpreter mutation");
      check(e.release(auth(123),later.ticket).status==Status::NoCommand,
            "observed release ticket is one use");
      check(!e.seal(auth(120),episode()),"released episode cannot reuse capture"); }
    for(int change=0;change<13;++change) {
      Endpoint e; latch_observed(e);
      const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      auto n=entry(122); auto now=auth(122);
      if(change==0) n.world_known=false;
      if(change==1) n.world_ready=false;
      if(change==2) n.position_known=false;
      if(change==3) ++n.position.x;
      if(change==4) n.static_quiet_known=false;
      if(change==5) n.static_npc_quiet=false;
      if(change==6) n.candidate_known=false;
      if(change==7) n.candidate_present=false;
      if(change==8) n.bag_known=false;
      if(change==9) n.bag=1;
      if(change==10) n.input_unused_known=false;
      if(change==11) n.input_unused=false;
      if(change==12) now=auth(123);
      check(!nomination_entry_allowed(r,now,n),
            "missing, false or stale independent caller-entry facts cannot release to Nominate");
    }
    for(int change=0;change<4;++change) {
      Endpoint e; latch_observed(e); auto o=facts(122);
      if(change==0) o.field.known=false;
      if(change==1) o.field.authority=auth(121);
      if(change==2) ++o.field.position.x;
      if(change==3) o.field.static_npc_quiet=false;
      check(e.observe_readiness(auth(122),frame(122),o).status==Status::NeedObservation,
            "current field fact must be known, same-authority and consistent with actual frame");
    }
    { Endpoint e; latch_observed(e); auto o=facts(122); o.cooldown_known={false,false};
      check(e.observe_readiness(auth(122),frame(122),o,full_ready()).reason==Reason::UnknownDependency,
            "observed Full schema retains both actual cooldown dependencies"); }
    { Endpoint e; latch_observed(e);
      check(e.observe_readiness(auth(122),frame(122),facts(122),{}).reason==Reason::UnsupportedSignature,
            "observed endpoint rejects unrepresented primitive dependency schema"); }
    { Endpoint e; auto goal=episode();goal.sealed.require_shop=true;latch_observed(e,goal);
      const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      check(r.status==Status::NeedsCapability && r.capability==Capability::Buy,
            "observed HoldCash cannot waive a required purchase"); }
    { Endpoint e; auto goal=episode();goal.terminal=Terminal::LegacySpawn;latch_observed(e,goal);
      const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      check(r.status==Status::NeedsCapability && r.capability==Capability::ReturnToSpawn,
            "observed API cannot waive original LegacySpawn requirement"); }
    { Endpoint e; latch_observed(e);auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      check(e.invalidate(auth(122,2),Reason::ActorDead).reason==Reason::ActorDead,
            "actual-death producer callback invalidates outstanding observed ticket");
      check(e.observe_readiness(auth(123),frame(123),facts(123)).status==Status::Failed,
            "later respawn/full HP cannot restore invalidated bank episode");
      check(!e.seal(auth(120),episode()) && r.ticket>0,
            "death cannot free episode/capture serial for resealing"); }
    { Endpoint e;latch_observed(e);auto f=frame(122);f.alive=false;f.hp=0;
      check(e.observe_readiness(auth(122),f,facts(122)).reason==Reason::ActorDead,
            "actual dead input directly invalidates observed episode"); }
    { Endpoint e;latch_observed(e);auto f=frame(122);f.bag=1;
      check(e.observe_readiness(auth(122),f,facts(122)).reason==Reason::Loaded,
            "observed readiness never reuses bank proof with newly loaded bag"); }
    { Endpoint e;auto goal=episode();goal.max_observation_calls=3;latch_observed(e,goal);
      const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      (void)e.continue_existing(auth(122),r.ticket);
      check(e.observe_readiness(auth(122),frame(122),facts(122)).reason==Reason::InputTicketConsumed,
            "repeated declined-input observation consumes retained finite polling allowance");
      check(e.observe_readiness(auth(123),frame(123),facts(123)).reason==Reason::WaitBound,
            "decline/continue cannot obtain a free new wait budget"); }
    { Endpoint e;auto goal=episode();goal.deadline_round=122;latch_observed(e,goal);
      const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      (void)e.continue_existing(auth(122),r.ticket);
      check(e.observe_readiness(auth(123),frame(123),facts(123)).reason==Reason::WaitBound,
            "declining a ticket preserves original absolute physical deadline"); }
    { Endpoint e;latch(e);const auto r=e.observe_prospective(auth(122),frame(122),facts(122),next_start(122));
      check(e.release(auth(122),r.ticket).reason==Reason::WrongTicketKind
            && e.continue_existing(auth(122),r.ticket).reason==Reason::WrongTicketKind,
            "prospective tickets cannot use observed release/continue APIs"); }
    { Endpoint e;latch_observed(e);const auto r=e.observe_readiness(auth(122),frame(122),facts(122));
      (void)e.continue_existing(auth(122),r.ticket);
      check(e.release(auth(122),r.ticket).reason==Reason::StaleTicket,
            "declined observed ticket cannot subsequently release continuation"); }
}
void positive_handoff_and_signatures() {
    { Endpoint e; latch(e); auto f = frame(122); f.gold = 837;
      auto o = facts(122); o.inventory.slots[0].count = 3;
      const auto ready = e.observe_prospective(auth(122),f,o,next_start(122));
      check(ready.status == Status::ReadyForNextOperator && ready.schema.family == Family::FullReadyField,
            "later fresh full field endpoint is sufficient, without spawn claim");
      check(ready.held.gold == 837 && ready.held.bag == 0 && ready.held.inventory.slots[0].count == 3
            && ready.held.authority == auth(122), "HoldCash preserves literal current resource snapshot");
      wb::service::Interpreter next;
      check(e.begin_next(auth(122),ready.ticket,next).status == Status::HandedOff
            && next.phase() == wb::service::Phase::Ingress, "actual immutable Interpreter begin starts new metadata episode");
      check(e.begin_next(auth(122),ready.ticket,next).status == Status::NoCommand,
            "ready ticket is one use");
      check(!e.seal(auth(120),episode()), "completed handoff cannot reuse old capture or episode"); }
    { Endpoint e; latch(e); auto f = frame(122); f.can_cast = false;
      auto o = facts(122); o.permissions_known[2] = false;
      o.cooldown_known = {false,false}; o.cooldown = {8,13};
      o.restrictions_known_mask = 3; o.restrictions = 4; // C restriction irrelevant to Bare
      auto n = next_start(122,bare_move_basic()); n.first.can_cast = false;
      auto r = e.observe_prospective(auth(122),f,o,n);
      check(r.status == Status::ReadyForNextOperator && r.schema.family == Family::BareMoveBasic,
            "BareMoveBasic requires M+A, not unused C or cooldowns"); }
    { Endpoint e; latch(e); auto o=facts(122);o.cooldown_known={false,false};
      auto f=frame(122);f.renewal_ready=true;
      check(e.observe_prospective(auth(122),f,o,next_start(122)).reason==Reason::UnknownDependency,
            "legacy renewal_ready flag never fills missing actual cooldown observations"); }
    { Endpoint e; latch(e); auto f = frame(122); f.can_cast = false;
      auto n = next_start(122); n.first.can_cast = false;
      check(e.observe_prospective(auth(122),f,facts(122),n).status == Status::NeedObservation,
            "FullReadyField still requires C"); }
    { Endpoint e; latch(e); auto n = next_start(122); n.schema = {};
      check(e.observe_prospective(auth(122),frame(122),facts(122),n).reason == Reason::UnsupportedSignature,
            "unknown primitive dependencies reject"); }
    { Endpoint e; latch(e); auto n = next_start(122);
      n.schema = {Family::ResetBasic,7,7,1,true};
      check(e.observe_prospective(auth(122),frame(122),facts(122),n).reason == Reason::UnsupportedSignature,
            "ResetBasic needs represented C slot and enhancement semantics, not Bare assumptions"); }
    { Endpoint e; latch(e); auto n = next_start(122,bare_move_basic()); n.schema.cooldowns = 1;
      check(e.observe_prospective(auth(122),frame(122),facts(122),n).reason == Reason::UnsupportedSignature,
            "unrepresented edited dependency schema cannot authorize"); }
}
void provenance_and_later_input() {
    { Endpoint e; latch(e); auto a = auth(121,2);
      check(e.observe_prospective(a,frame(121,2),facts(121,2),next_start(121,full_ready(),2)).reason == Reason::NotLaterInput,
            "revision refresh on bank input is not later physical renewal"); }
    for (int change = 0; change < 8; ++change) {
        Endpoint e; check(e.seal(auth(120),episode()), "bank negative seals");
        auto b = bank(); auto f = frame(121);
        if(change==0) b.source=ProofSource::TeamCounter;
        if(change==1) ++b.episode;
        if(change==2) ++b.capture_serial;
        if(change==3) b.owner=2;
        if(change==4) b.group=3;
        if(change==5) b.receipt.serial=10;
        if(change==6) b.receipt.gems=29;
        if(change==7) f.own_bank_receipt.serial=12;
        f.submitted_known=true;f.submitted_counter=1000; // NEVER a substitute proof
        check(e.observe_prospective(auth(121),f,facts(121),next_start(121),&b).reason==Reason::WrongBankProvenance,
              "wrong or team-only receipt provenance cannot settle this episode");
    }
    { Endpoint e; latch(e); auto f=frame(122); f.authority=auth(121);
      check(e.observe_prospective(auth(122),f,facts(122),next_start(122)).status==Status::NeedObservation,
            "independently current authority rejects stale frame"); }
    { Endpoint e; latch(e); auto o=facts(122); o.episode_continuity_known=false;
      check(e.observe_prospective(auth(122),frame(122),o,next_start(122)).status==Status::Failed,
            "alive snapshot without episode continuity cannot reuse historical bank"); }
}
void partial_facts_and_readiness() {
    for (int change=0;change<18;++change) {
        Endpoint e; latch(e); auto f=frame(122);auto o=facts(122);auto n=next_start(122);
        if(change==0) f.fresh=false;
        if(change==1) o.self_known=false;
        if(change==2) o.life_known=false;
        if(change==3) o.health_known=false;
        if(change==4) o.position_known=false;
        if(change==5) o.bag_known=false;
        if(change==6) o.gold_known=false;
        if(change==7) o.channel_known=false;
        if(change==8) o.stun_known=false;
        if(change==9) o.permissions_known[0]=false;
        if(change==10) o.permissions_known[1]=false;
        if(change==11) o.permissions_known[2]=false;
        if(change==12) o.cooldown_known[0]=false;
        if(change==13) o.cooldown_known[1]=false;
        if(change==14) o.restrictions_known_mask=3;
        if(change==15) o.inventory.complete=false;
        if(change==16) o.input_unused_known=false;
        if(change==17) o.inventory.authority=auth(121);
        check(e.observe_prospective(auth(122),f,o,n).status!=Status::ReadyForNextOperator,
              "partial freshness/dependency/resource facts cannot certify endpoint");
    }
    for (int change=0;change<10;++change) {
        Endpoint e; latch(e);auto f=frame(122);auto o=facts(122);auto n=next_start(122);
        if(change==0) {f.hp=1199;n.first.hp=1199;}
        if(change==1) f.max_hp=0;
        if(change==2) f.hp=std::numeric_limits<float>::quiet_NaN();
        if(change==3) o.stunned=true;
        if(change==4) o.restrictions=1;
        if(change==5) o.cooldown[0]=1;
        if(change==6) o.cooldown[1]=7;
        if(change==7) f.channel=wb::service::Channel::Recall;
        if(change==8) o.input_unused=false; // a false attempt can leave all MAC ready
        if(change==9) {f.can_attack=false;n.first.can_attack=false;}
        check(e.observe_prospective(auth(122),f,o,n).status!=Status::ReadyForNextOperator,
              "known nonready condition is never promoted to readiness");
    }
}
void next_operator_and_capabilities() {
    for(int change=0;change<10;++change) {
        Endpoint e; latch(e);auto n=next_start(122);
        if(change==0) n.scope=Scope::Unknown;
        if(change==1) n.scoped_assumptions_certified=false;
        if(change==2) n.whole_group_harmless=false;
        if(change==3) n.static_quiet_origin=false;
        if(change==4) n.bounded_continuation_prepared=false;
        if(change==5) n.first.authority=auth(121);
        if(change==6) n.contract.owner=2;
        if(change==7) n.first.target_visible=false;
        if(change==8) n.primitive.kind=wb::service_micro::Kind::Cast;
        if(change==9) n.next_episode=5;
        check(e.observe_prospective(auth(122),frame(122),facts(122),n).status!=Status::ReadyForNextOperator,
              "next basic start is explicit; no visible foes is no scope certificate");
    }
    { Endpoint e;auto goal=episode();goal.sealed.require_shop=true;latch(e,goal);
      const auto r=e.observe_prospective(auth(122),frame(122),facts(122),next_start(122));
      check(r.status==Status::NeedsCapability && r.capability==Capability::Buy,
            "HoldCash never completes original required shop"); }
    { Endpoint e;auto goal=episode();goal.terminal=Terminal::LegacySpawn;latch(e,goal);
      const auto r=e.observe_prospective(auth(122),frame(122),facts(122),next_start(122));
      check(r.status==Status::NeedsCapability && r.capability==Capability::ReturnToSpawn,
            "field endpoint does not retroactively weaken implicit d01 spawn terminal"); }
    { Endpoint e;latch(e);auto n=next_start(122);n.primitive={wb::service_micro::Kind::Move,-1,{9,8}};
      n.first.observed_move=n.first.move_legal=true;n.first.move_endpoint={9,8};
      n.first.move_landing=wb::service_micro::MoveLanding::MayRefuse;
      n.first.refusal_prefix_supported=true;
      check(e.observe_prospective(auth(122),frame(122),facts(122),n).status==Status::ReadyForNextOperator,
            "first next move may use explicit bounded quiet refusal prefix"); }
}
void persistent_closure() {
    { Endpoint e;latch(e);auto f=frame(122);f.alive=false;f.hp=0;
      check(e.observe_prospective(auth(122),f,facts(122),next_start(122)).reason==Reason::ActorDead,
            "actual death invalidates endpoint episode");
      check(e.observe_prospective(auth(123),frame(123),facts(123),next_start(123)).status==Status::Failed,
            "later full HP cannot silently revive failed episode"); }
    { Endpoint e;latch(e);auto f=frame(122);f.bag=1;
      check(e.observe_prospective(auth(122),f,facts(122),next_start(122)).reason==Reason::Loaded,
            "new loaded bag is not empty-bank endpoint"); }
    { Endpoint e;auto goal=episode();goal.max_observation_calls=3;latch(e,goal);
      auto o=facts(122);o.cooldown_known[0]=false;
      check(e.observe_prospective(auth(122),frame(122),o,next_start(122)).status==Status::NeedObservation,
            "first unknown-CD observation uses retained allowance");
      check(!e.seal(auth(120),goal),"active re-contract cannot reset wait allowance");
      check(e.observe_prospective(auth(122),frame(122),o,next_start(122)).status==Status::NeedObservation,
            "same-input repeated unknown facts remain bounded");
      check(e.observe_prospective(auth(122),frame(122),o,next_start(122)).reason==Reason::WaitBound,
            "finite observation budget closes unchanged waiting");
      check(!e.seal(auth(120),goal),"failed same episode cannot reset allowance");
      auto forged=goal;++forged.id;forged.opened_at=auth(123);forged.deadline_round=160;
      check(!e.seal(auth(123),forged),"new label with same capture serial cannot reset failure"); }
    { Endpoint e;auto goal=episode();goal.deadline_round=124;latch(e,goal);
      check(e.observe_prospective(auth(125),frame(125),facts(125),next_start(125)).reason==Reason::WaitBound,
            "absolute physical deadline prices unobserved skipped rounds"); }
    { Endpoint e;auto goal=episode();goal.deadline_round=124;latch(e,goal);
      check(e.observe_prospective(auth(125),frame(122),facts(122),next_start(122)).reason==Reason::WaitBound,
            "partial stale frame cannot hide independently known expired deadline"); }
    { Endpoint e;auto goal=episode();goal.deadline_round=122;latch(e,goal);
      (void)e.observe_prospective(auth(123),frame(123),facts(123),next_start(123));
      auto genuine=episode();genuine.id=6;genuine.capture.serial=21;genuine.bank_serial_baseline=11;
      genuine.opened_at=auth(124);genuine.deadline_round=150;
      check(e.seal(auth(124),genuine),"genuinely new capture/episode may establish a new finite budget");
      auto b=bank();b.authority=auth(125);b.episode=6;b.capture_serial=21;b.receipt.serial=12;
      auto f=frame(125);f.own_bank_receipt.serial=12;
      check(e.observe_prospective(auth(125),f,facts(125),next_start(125),&b).status==Status::BankVerified,
            "new episode needs new exact own-bank serial");
      f=frame(126);f.own_bank_receipt.serial=12;auto n=next_start(126);n.next_episode=7;
      check(e.observe_prospective(auth(126),f,facts(126),n).status==Status::ReadyForNextOperator,
            "real new receipt can progress without relabeling an unchanged failed word"); }
    { Endpoint e;latch(e);auto r=e.observe_prospective(auth(122),frame(122),facts(122),next_start(122));
      wb::service::Interpreter next;
      check(e.begin_next(auth(123),r.ticket,next).reason==Reason::StaleTicket,
            "later input requires new endpoint proof rather than stale ticket"); }
    { Endpoint e;latch(e);auto r=e.observe_prospective(auth(122),frame(122),facts(122),next_start(122));
      wb::service::Interpreter already;check(already.begin(next_start(122).contract),"prepare an active next interpreter");
      check(e.begin_next(auth(122),r.ticket,already).reason==Reason::NextBeginRefused,
            "metadata handoff cannot reset an active Interpreter"); }
}
}
int main() {
    static_assert(!std::is_copy_constructible_v<Endpoint> && !std::is_move_constructible_v<Endpoint>);
    positive_handoff_and_signatures();provenance_and_later_input();partial_facts_and_readiness();
    next_operator_and_capabilities();persistent_closure();
    observed_endpoint_and_caller();
    std::printf("service_ready_field: %d checks, %d failures; no native/core qualification\n",checks,failures);
    return failures ? 1 : 0;
}
