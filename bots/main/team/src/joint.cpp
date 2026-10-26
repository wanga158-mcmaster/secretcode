#include "joint.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "combat.h"
#include "config.h"
#include "ctx.h"
#include "world.h"
#include "joint_audit.h"

namespace mb::joint {
namespace {
// Per-instance state, deliberately outside both existing class layouts. An exact pending
// respawn end is retained for every encodable group, including transient cleared writes.
struct Runtime {
    Packet wire{}, request{}, plan{}, seen_abort{};
    bool have_request{}, have_plan{}, seen_plan{}, suppress{}, latent_valid{};
    bool abort_seen{};
    std::uint8_t latent{};
    int issue{-100}, freeze_until{-100}, lock_until{-100}, expiry{-100};
    int cooldown{-100}, last_group{-1}, group_until{-100}, turn{-1};
    int pending[63]{};
    bool veto{}, capture{}, arrived{};
    float gem_weight{1.0f};
    float farm_gold_mult{1.0f}, farm_gem_gold{};
    Counters snapshot{};
    bool snapshot_ready{}, emitted{};
    Counters counts{};
};
Runtime& state() { static Runtime s; return s; }
int completion_window() {
    return std::max(1,static_cast<int>(cambc::constants::capture_time))+
           std::max(1,static_cast<int>(cambc::constants::recall_time))+
           std::max(1,static_cast<int>(cambc::constants::camp_sleep_time))+1;
}
Packet read_wire() {
    std::array<std::byte,3> raw{};
    cambc::shared_memory.read(5, std::span<std::byte>{raw.data(),raw.size()});
    Wire w{};
    for (int i=0;i<3;++i) w[i]=std::to_integer<std::uint8_t>(raw[i]);
    return decode(w);
}
void write_wire(const Packet& p) {
    auto& s=state();
    if (p.tag!=Tag::news && s.wire.tag==Tag::news && s.wire.group>=0 && s.wire.until>Knowledge::get().round)
        s.pending[s.wire.group]=std::max(s.pending[s.wire.group],s.wire.until);
    Wire w{}; if (!encode(p,w)) return;
    std::array<std::byte,3> raw{};
    for (int i=0;i<3;++i) raw[i]=static_cast<std::byte>(w[i]);
    cambc::shared_memory.write(5,std::span<const std::byte>{raw.data(),raw.size()});
    s.wire=p;
}
void restore() {
    auto& s=state();
    if (!s.latent_valid) return;
    const std::byte b{ s.latent };
    cambc::shared_memory.write(static_cast<std::size_t>(Knowledge::get().self_index),std::span<const std::byte>{&b,1});
}
bool locked(int now) {
    const auto& s=state();
    return (s.wire.tag==Tag::request && now<=s.wire.round+2) || now<=s.lock_until;
}
void abort_request(int now) {
    auto& s=state();
    Packet p=s.request; p.tag=Tag::abort;
    write_wire(p); s.have_plan=false;
    s.issue=p.round+2; s.freeze_until=s.issue+1; s.lock_until=std::max(s.freeze_until,now+1);
    s.seen_abort=p; s.abort_seen=true;
    s.cooldown=std::max(s.cooldown,now+static_cast<int>(cambc::constants::capture_time)+2);
    ++s.counts.aborts; restore();
}
float reward(const Ctx& ctx,int g) {
    const auto& info=World::get().groups[static_cast<std::size_t>(g)];
    // Use the same common gem/gold units for task reward and the farming upper bound.
    return ctx.gem_weight*static_cast<float>(info.gems)+cfg::gold_value*static_cast<float>(info.gold);
}
int nearest(vec2 p,int g) {
    const auto& info=World::get().groups[static_cast<std::size_t>(g)];
    return std::max(std::max({0,info.min_x-p.x,p.x-info.max_x}),std::max({0,info.min_y-p.y,p.y-info.max_y}));
}
float farm_price(const Ctx& ctx) {
    float upper=0.0f;
    audit::FarmTerm audit_term;
    int audit_group=-1;
    if constexpr (cfg::joint_admission_audit) {
        audit_term.sampled=true;
        if (audit::capturing(state().request)) audit_group=audit::record().request.group;
    }
    const auto& kn=Knowledge::get(); const auto& world=World::get();
    const int capture=std::max(1,static_cast<int>(cambc::constants::capture_time));
    for (std::size_t g=0;g<world.groups.size();++g) {
        if (kn.groups[g].status==GroupStatus::respawning && kn.groups[g].respawn_round>ctx.round+14) continue;
        // Chebyshev to a member is a LOWER bound on walking, making this reward/time an
        // upper farming price. Ignoring fight time and sharing is deliberately conservative.
        const auto& info=world.groups[g];
        const int dx=std::max({0,info.min_x-ctx.pos.x,ctx.pos.x-info.max_x});
        const int dy=std::max({0,info.min_y-ctx.pos.y,ctx.pos.y-info.max_y});
        const int travel=std::max(0,std::max(dx,dy)-1); // O(1) lower bound, even on many-member groups
        const float farm_reward=(ctx.gem_weight+cfg::gold_value*state().farm_gem_gold)*static_cast<float>(info.gems)+
            cfg::gold_value*state().farm_gold_mult*static_cast<float>(info.gold);
        const float rate=farm_reward/static_cast<float>(travel+capture);
        upper=std::max(upper,rate);
        if constexpr (cfg::joint_admission_audit)
            if (static_cast<int>(g)==audit_group) {
                audit_term.included=true; audit_term.requested_reward=farm_reward;
                audit_term.requested_travel=travel; audit_term.requested_term=rate;
            }
    }
    if constexpr (cfg::joint_admission_audit) audit::farm_sample(state().request,audit_term);
    return upper;
}
bool immediate_capture(const Ctx& ctx) {
    const auto& kn=Knowledge::get(); const int g=World::get().group_at(ctx.pos);
    return g>=0 && kn.groups[static_cast<std::size_t>(g)].status==GroupStatus::reward;
}
bool channel(const Ctx& ctx) {
    return ctx.state.contains(cambc::champion_state::recalling) ||
           ctx.state.contains(cambc::champion_state::submitting_gems) ||
           ctx.state.contains(cambc::champion_state::teleporting);
}
void flush_news(Knowledge& kn) {
    auto& s=state(); if (locked(kn.round)) return;
    int best=-1; float score=-1.0f;
    auto urgency=[&](int g) {
        for (std::size_t i=0;i<kn.slots().size();++i)
            if (static_cast<int>(i)!=kn.self_index && kn.alive[i] && kn.slots()[i].group==g && kn.slots()[i].status==SlotStatus::engaging) return true;
        return false;
    };
    const bool protected_news=s.wire.tag==Tag::news && s.wire.group>=0 && s.wire.until>kn.round && urgency(s.wire.group);
    for (std::size_t g=0;g<kn.groups.size();++g) {
        if (s.pending[g]<=kn.round) { s.pending[g]=0; continue; }
        if (protected_news && !urgency(static_cast<int>(g))) continue;
        const auto& info=World::get().groups[g];
        const float v=(urgency(static_cast<int>(g))?1e6f:0.0f)+static_cast<float>(info.gems+info.gold);
        if (v>score) { score=v; best=static_cast<int>(g); }
    }
    if (best>=0) {
        Packet p; p.tag=Tag::news; p.group=best; p.until=s.pending[best];
        write_wire(p); s.pending[best]=0;
    }
}
}

bool enabled() {
    if constexpr (!cfg::joint_tasks) return false;
    static const bool ok=cambc::shared_memory_buffer::size()==8 && Knowledge::get().allies.size()<=5 &&
        World::get().groups.size()<=63 && cambc::constants::max_rounds<=2047 &&
        cfg::shared_capture_confirmed_only && !cfg::share_sightings && cfg::phantom_mode!=2 && cfg::sos_radius==0;
    return ok;
}
Counters& counters() { return state().counts; }
bool frozen() { return state().suppress; }
bool tagged_news() { return state().wire.tag!=Tag::news; }
bool execution() {
    // Both comparison values are nonzero data, so execution-only controls retain code layout.
    static volatile std::uint32_t gate=cfg::joint_execute?1u:2u;
    return gate==1u;
}
void begin(Knowledge& kn) {
    auto& s=state(); const int now=kn.round; s.turn=now; s.veto=false; s.capture=false;
    if constexpr (cfg::joint_census)
        if (!s.snapshot_ready && now>=151) { s.snapshot=s.counts; s.snapshot_ready=true; }
    s.wire=read_wire();
    if (s.wire.tag==Tag::request && s.wire.group>=0 && s.wire.round<=now && s.wire.owner>=0 && s.wire.owner<static_cast<int>(kn.allies.size())) {
        s.request=s.wire; s.have_request=true; s.issue=s.request.round+2;
        if constexpr (cfg::joint_admission_audit) audit::start(s.request,kn.self_index,static_cast<int>(kn.allies.size()));
        s.freeze_until=s.issue+1; s.lock_until=s.freeze_until;
        if (now<=s.issue && !(now==s.issue && kn.self_index==s.request.owner)) {
            // A failed/incomplete owner turn must not leave its earlier bid certified. Other
            // actors can only consume completed-turn bids from this same cleared auction.
            const std::byte invalid{0};
            cambc::shared_memory.write(static_cast<std::size_t>(kn.self_index),std::span<const std::byte>{&invalid,1});
            if constexpr (cfg::joint_admission_audit)
                audit::bid(s.request,now,0,0,-1,0.0f,-1,-1,-1,audit::incomplete,audit::incomplete,0);
        }
        if (now>s.issue) {
            abort_request(now); // absent owner cannot hold the mailbox indefinitely
            if constexpr (cfg::joint_admission_audit)
                audit::result(s.request,now,audit::owner_missing,static_cast<int>(Tag::abort),kn.self_index==s.request.owner);
        }
    } else if (s.wire.tag==Tag::commit && s.wire.group>=0 && s.wire.mask!=0) {
        const bool fresh=!s.seen_plan || s.plan.group!=s.wire.group || s.plan.deadline!=s.wire.deadline || s.plan.mask!=s.wire.mask;
        if (fresh) {
            // A bidder has the request context; an unrelated late observer protects one full
            // following round from first observation. Exact absolute deadlines never wrap.
            s.issue=s.have_request && s.request.group==s.wire.group && s.request.round+2<=now ? s.request.round+2:now;
            s.freeze_until=s.issue+1; s.lock_until=s.freeze_until;
            s.plan=s.wire; s.have_plan=true; s.seen_plan=true; s.arrived=false;
            if (s.plan.mask&(1u<<kn.self_index)) ++s.counts.assigned;
            else ++s.counts.farm;
            s.expiry=std::min(static_cast<int>(cambc::constants::max_rounds),s.plan.deadline+completion_window());
            s.cooldown=s.expiry;
        }
        restore();
        if constexpr (cfg::joint_admission_audit)
            if (s.have_request && s.request.group==s.wire.group && now>=s.request.round+2 && now<=s.request.round+4)
                audit::result(s.request,now,audit::observed_commit,static_cast<int>(Tag::commit));
    } else if (s.wire.tag==Tag::abort && s.wire.round<=now) {
        const bool fresh=!s.abort_seen || s.seen_abort.group!=s.wire.group || s.seen_abort.round!=s.wire.round || s.seen_abort.owner!=s.wire.owner;
        s.issue=s.wire.round+2; s.freeze_until=s.issue+1;
        if (fresh) { s.lock_until=std::max(s.freeze_until,now+1); s.seen_abort=s.wire; s.abort_seen=true; }
        s.have_plan=false; s.cooldown=std::max(s.cooldown,s.issue+std::max(1,static_cast<int>(cambc::constants::capture_time)));
        restore();
        if constexpr (cfg::joint_admission_audit)
            audit::result(s.wire,now,audit::observed_abort,static_cast<int>(Tag::abort));
    }
    s.suppress=now<=s.freeze_until;
    if (!s.suppress && s.have_request) { restore(); s.have_request=false; }
    if (s.have_plan) {
        bool dead=false;
        for (std::size_t i=0;i<kn.allies.size();++i)
            if ((s.plan.mask&(1u<<i)) && !kn.allies[i].is_alive()) dead=true;
        if (now>s.expiry) ++s.counts.expired;
        if (dead || now>s.expiry) stop();
    }
    flush_news(kn);
}
bool publication(int group,SlotStatus status) {
    auto& s=state();
    s.latent=static_cast<std::uint8_t>(((group+1)&63)|(static_cast<unsigned>(status)<<6)); s.latent_valid=true;
    if (s.suppress && group>=0 && status==SlotStatus::cleared) {
        const auto& kn=Knowledge::get(); const auto& b=kn.groups[static_cast<std::size_t>(group)];
        const int until=b.status==GroupStatus::respawning?b.respawn_round:kn.round+World::get().groups[static_cast<std::size_t>(group)].respawn;
        news(group,until);
    }
    return s.suppress && s.wire.tag==Tag::request;
}
bool news(int group,int until) {
    auto& s=state();
    if (group<0 || group>=63 || until<0 || until>65535) return false;
    if (!locked(Knowledge::get().round)) {
        // The caller immediately emits these exact original C2 bytes. Keep our mirror
        // current in case an auction leases the mailbox later in the same owner turn.
        s.wire=Packet{}; s.wire.tag=Tag::news; s.wire.group=group; s.wire.until=until;
        return false;
    }
    s.pending[group]=std::max(s.pending[group],until); return true;
}
bool winning_bank(const Ctx& ctx) {
    return ctx.gems>0 && cambc::global_submitted_gems(cambc::team::ally)+ctx.gems>=cambc::constants::winning_gems;
}
bool arrived() { return state().arrived; }
void mark_arrived() {
    auto& s=state();
    if (!s.arrived) {
        ++s.counts.arrivals;
        if (Knowledge::get().self_index!=s.request.owner) ++s.counts.helper_arrivals;
    }
    s.arrived=true;
}
const Packet* task() {
    const auto& s=state();
    return s.have_plan && (s.plan.mask&(1u<<Knowledge::get().self_index)) ? &s.plan:nullptr;
}
void stop() {
    auto& s=state();
    if (s.have_plan) { s.last_group=s.plan.group; s.group_until=std::max(s.group_until,s.expiry); }
    s.have_plan=false; ++s.counts.rejects;
}
void veto() {
    auto& s=state();
    if (!task()) return; // another hero's capture/channel is not a coalition veto
    if (s.have_plan && s.request.round>=0 && s.request.group==s.plan.group) {
        // Cancel the whole coalition when a promised member cannot honour its assignment.
        // Preserve the original request stamp: late aborts do not restart the borrow phase.
        Packet p=s.request; p.tag=Tag::abort; write_wire(p);
        s.seen_abort=p; s.abort_seen=true; s.lock_until=std::max(s.lock_until,s.turn+1);
    }
    stop();
}
bool rejected_group(int group) {
    const auto& s=state();
    if (!execution()) return false;
    if (group==s.last_group && s.turn<=s.group_until) return true;
    const bool sleeping=group>=0 && !Knowledge::get().groups[static_cast<std::size_t>(group)].awake;
    return group>=0 && ((s.have_request && s.turn<=s.issue+1 && group==s.request.group && sleeping) ||
                      (s.have_plan && group==s.plan.group));
}

void observe(const Ctx& ctx,int farm_group,bool capturing,float farm_gold_mult,float farm_gem_gold) {
    auto& s=state(); auto& kn=Knowledge::get(); auto& world=World::get();
    s.capture=capturing; s.veto=capturing || immediate_capture(ctx) || channel(ctx) || winning_bank(ctx);
    s.gem_weight=ctx.gem_weight;
    s.farm_gold_mult=farm_gold_mult; s.farm_gem_gold=farm_gem_gold;
    if (task() && s.veto) veto();
    if (s.have_plan && kn.groups[static_cast<std::size_t>(s.plan.group)].status==GroupStatus::respawning) stop();
    if (!s.veto && s.wire.tag==Tag::request && s.have_request && ctx.round<=s.issue &&
        !world.group_field(s.request.group,false) &&
        fuel_ok(world.field_cost_at(world.groups[static_cast<std::size_t>(s.request.group)].center)+cfg::fuel_phase_farm+8000))
        world.group_field(s.request.group,true,cfg::fuel_phase_farm+8000);
    if (s.wire.tag==Tag::request && s.have_request && ctx.round==s.issue && s.request.owner==kn.self_index) {
        if (s.veto || !fuel_ok(cfg::fuel_phase_farm+9000)) {
            abort_request(ctx.round);
            if constexpr (cfg::joint_admission_audit)
                audit::result(s.request,ctx.round,s.veto?audit::veto:audit::choose_fuel,static_cast<int>(Tag::abort),true);
            return;
        }
        Input in; in.now=ctx.round; in.requester=kn.self_index; in.roster_size=static_cast<int>(kn.allies.size());
        // Hand-seeded urgency horizon caps; the live damage budget below may tighten them.
        const int window=s.request.urgency>=3?2:s.request.urgency==2?4:s.request.urgency==1?8:12;
        in.deadline=std::min(static_cast<int>(cambc::constants::max_rounds),ctx.round+window);
        in.capture_rounds=std::max(1,static_cast<int>(cambc::constants::capture_time));
        in.completion_rounds=completion_window();
        in.horizon=std::min({256,window+in.completion_rounds,static_cast<int>(cambc::constants::max_rounds)-ctx.round});
        in.recall_cycle=static_cast<int>(cambc::constants::recall_time)+1;
        in.death_gold=static_cast<float>(cambc::constants::kill_gold);
        in.alive_mask=0;
        std::array<std::byte,5> raw{}; cambc::shared_memory.read(0,std::span<std::byte>{raw.data(),kn.allies.size()});
        const auto& info=world.groups[static_cast<std::size_t>(s.request.group)];
        const auto& belief=kn.groups[static_cast<std::size_t>(s.request.group)];
        in.reward=reward(ctx,s.request.group); in.camp_hp=belief.hp_left>=0?belief.hp_left:info.total_hp;
        in.camp_dps=info.total_strength; in.requester_hp=ctx.hp;
        in.threat_dps=camp_threat_at(ctx,ctx.pos)+enemy_threat_at(ctx,ctx.pos);
        if (in.threat_dps>0) {
            const int survival=std::max(0,static_cast<int>(std::ceil(ctx.hp/(in.threat_dps*in.safety_margin)))-1);
            in.deadline=std::min(in.deadline,ctx.round+std::min(14,survival));
        }
        for (std::size_t i=0;i<kn.allies.size();++i) {
            if (kn.allies[i].is_alive()) in.alive_mask|=static_cast<std::uint8_t>(1u<<i);
            in.bids[i]=decode_bid(std::to_integer<std::uint8_t>(raw[i]));
            auto summary=design_summary(kn.allies[i].champion_name());
            // Remote bids certify a healthy volunteer; immutable design power is a conservative
            // estimate. Visible actors use their observed stats, not unseen host queries.
            in.hp[i]=summary.health*cfg::recall_health_frac;
            for (const auto& a:ctx.allies) if (a.champ && *a.champ==kn.allies[i]) {
                // Ally Ctx views do not ordinarily populate strength/focus. Keep design
                // lower bounds rather than overwriting power with those zero placeholders.
                summary.strength=std::max(summary.strength,a.strength);
                summary.focus=std::max(summary.focus,a.focus); in.hp[i]=a.hp;
            }
            if (static_cast<int>(i)==kn.self_index) { summary.strength=ctx.strength; summary.focus=ctx.focus; in.hp[i]=ctx.hp; }
            in.dps[i]=model::camp_dps(summary,info.neighbours,cfg::camp_dash_damage);
        }
        // The coordinator may have used immediate safety actions before planning. Price
        // its actual present position rather than trusting its prior end-turn location.
        const auto* own_field=world.group_field(s.request.group,false);
        const int own_eta=own_field?static_cast<int>(own_field->get(ctx.pos)):15;
        const float own_price=farm_price(ctx);
        const auto own_raw=encode_bid(own_eta,own_price,!s.veto);
        in.bids[static_cast<std::size_t>(kn.self_index)]=decode_bid(own_raw);
        unsigned long long audit_fuel=0;
        if constexpr (cfg::joint_admission_audit) if (audit::capturing(s.request)) audit_fuel=fuel();
        const Choice choice=choose(in); // fixed <=31 masks, independent of coefficient zeros
        auto audit_choice=[&]() {
            if constexpr (cfg::joint_admission_audit) {
                if (!audit::capturing(s.request)) return;
                unsigned reasons=(s.veto?audit::phase_veto:0u)|(!own_field?audit::no_field:0u)|
                    ((own_eta<0 || own_eta>14)?audit::eta_range:0u)|
                    ((!std::isfinite(own_price) || own_price<0 || own_price>kPriceUpper.back())?audit::price_range:0u);
                audit::bid(s.request,ctx.round,2,own_raw,own_eta,own_price,ctx.pos.x,ctx.pos.y,own_field?1:0,
                    reasons,audit::phase_veto|audit::no_field|audit::eta_range|audit::price_range,7);
                std::array<int,5> saved_raw{};
                for (std::size_t i=0;i<kn.allies.size();++i) saved_raw[i]=std::to_integer<std::uint8_t>(raw[i]);
                audit::chosen(s.request,ctx.round,in,choice,saved_raw,audit_fuel);
            }
        };
        if (!choice.mask) { ++s.counts.farm; abort_request(ctx.round); audit_choice(); return; }
        Packet p; p.tag=Tag::commit; p.group=s.request.group; p.deadline=choice.deadline; p.mask=choice.mask;
        write_wire(p); s.plan=p; s.have_plan=true; s.seen_plan=true; s.arrived=false;
        ++s.counts.assigned;
        s.expiry=std::min(static_cast<int>(cambc::constants::max_rounds),p.deadline+completion_window());
        s.cooldown=s.expiry; ++s.counts.commits; restore(); audit_choice(); return;
    }
    if (s.have_plan || s.veto || s.suppress || locked(ctx.round) || ctx.round<=s.cooldown || farm_group<0 ||
        farm_group>=static_cast<int>(world.groups.size()) || !fuel_ok(cfg::fuel_phase_farm+8000)) return;
    const auto& b=kn.groups[static_cast<std::size_t>(farm_group)];
    if (b.status!=GroupStatus::alive || rejected_group(farm_group)) return;
    const auto& info=world.groups[static_cast<std::size_t>(farm_group)];
    const int distance=nearest(ctx.pos,farm_group);
    if (distance>kit().vision || b.seen_round!=ctx.round) return;
    auto summary=kit().summary; summary.strength=ctx.strength; summary.focus=ctx.focus;
    const float dps=model::camp_dps(summary,info.neighbours,cfg::camp_dash_damage);
    const auto forecast=model::forecast_from_constants({info.total_hp,info.weighted_hp,info.total_strength},dps,
        b.hp_left>=0 && info.total_hp>0?b.hp_left/info.total_hp:1.0f);
    const float threat=camp_threat_at(ctx,ctx.pos)+enemy_threat_at(ctx,ctx.pos);
    // Ask before waking a camp whose planned solo force is insufficient, or promptly on
    // observed damage/contact. The target itself supplies the shared rendezvous endpoint.
    if (forecast.damage*cfg::camp_safety_margin<ctx.hp*(1.0f-cfg::recall_health_frac) && threat<=0 && ctx.enemies.empty() && !ctx.hit_unseen) return;
    Packet p; p.tag=Tag::request; p.group=farm_group; p.round=ctx.round; p.owner=kn.self_index;
    p.urgency=threat>0 && threat*4>=ctx.hp?3:(!ctx.enemies.empty() || ctx.hit_unseen)?2:threat>0?1:0;
    Wire w{}; if (!encode(p,w)) return;
    if (s.wire.tag==Tag::news && s.wire.group>=0 && s.wire.until>ctx.round)
        s.pending[s.wire.group]=std::max(s.pending[s.wire.group],s.wire.until);
    std::array<std::byte,8> raw{}; // zero = invalid bid, never a cleared/capturing announcement
    for (int i=0;i<3;++i) raw[5+i]=static_cast<std::byte>(w[i]);
    cambc::shared_memory.write(0,std::span<const std::byte>{raw.data(),raw.size()});
    s.wire=s.request=p; s.have_request=true; s.issue=ctx.round+2; s.freeze_until=s.issue+1; s.lock_until=s.freeze_until;
    s.suppress=true;
    s.group_until=ctx.round+std::max(1,static_cast<int>(cambc::constants::capture_time))+static_cast<int>(cambc::constants::recall_time)+12;
    s.cooldown=s.group_until; ++s.counts.requests;
    if constexpr (cfg::joint_admission_audit) audit::start(s.request,kn.self_index,static_cast<int>(kn.allies.size()));
}

void emit_admission_audit() {
    if constexpr (cfg::joint_admission_audit) {
        if (!audit::started()) return; // do not initialize the buffered record before a request
        const auto& r=audit::record();
        if (!r.seen || r.emitted) return;
        const int now=static_cast<int>(cambc::round_number());
        if (audit::ready(now) && fuel()>95000) audit::emit(now,r.self,r.roster);
    }
}

void finish(int farm_group,bool capturing) {
    if (!enabled()) return;
    auto& s=state(); auto& kn=Knowledge::get(); const int now=static_cast<int>(cambc::round_number());
    auto publish_bid=[&]() {
    if (s.turn!=now || s.wire.tag!=Tag::request || !s.have_request || now>s.issue) return;
    // An owner occupied by a channel/death/early return must still resolve its lease on r+2.
    if (now==s.issue && s.request.owner==kn.self_index) {
        abort_request(now);
        if constexpr (cfg::joint_admission_audit)
            audit::result(s.request,now,audit::owner_early_return,static_cast<int>(Tag::abort),true);
        return;
    }
    const std::byte invalid{0};
    const auto offset=static_cast<std::size_t>(kn.self_index);
    const bool bid_alive=cambc::self().is_alive();
    if (!bid_alive || !fuel_ok(8000)) {
        cambc::shared_memory.write(offset,std::span<const std::byte>{&invalid,1});
        if constexpr (cfg::joint_admission_audit)
            audit::bid(s.request,now,1,0,-1,0.0f,-1,-1,-1,bid_alive?audit::bid_fuel:audit::dead,
                audit::dead|(bid_alive?audit::bid_fuel:0u),0);
        return;
    }
    Ctx ctx; ctx.round=now; ctx.refresh_self();
    ctx.gems=cambc::self().gem_count(); ctx.gem_weight=s.gem_weight;
    unsigned rejected=0,checked=0;
    auto admit=[&](bool pass,unsigned reason) {
        if constexpr (cfg::joint_admission_audit) { checked|=reason; if (!pass) rejected|=reason; }
        return pass;
    };
    bool eligible=admit(!capturing,audit::capture) && admit(!s.veto,audit::phase_veto) &&
        admit(!channel(ctx),audit::channel) && admit(!winning_bank(ctx),audit::winning_bank) &&
        admit(!immediate_capture(ctx),audit::immediate_capture) && admit(ctx.hp_frac()>=cfg::recall_health_frac,audit::low_hp);
    const auto* field=World::get().group_field(s.request.group,false);
    // Only the actual static field certifies an ETA. Optional warming uses the existing
    // budgeted SDK path and is never attempted in this end-of-turn bid path.
    int eta=15;
    if (field) {
        const int raw=static_cast<int>(field->get(ctx.pos));
        // Full path distance is conservative. Subtracting range can cut through a wall,
        // so it cannot certify arrival at a legal visible attack endpoint.
        if (raw>=0 && raw<(1<<14)) eta=raw;
    }
    if (eta>14) eligible=false;
    const float price=farm_price(ctx);
    const std::byte bid{encode_bid(eta,price,eligible)};
    cambc::shared_memory.write(offset,std::span<const std::byte>{&bid,1}); ++s.counts.bids;
    if constexpr (cfg::joint_admission_audit) {
        checked|=audit::no_field|audit::eta_range|audit::price_range;
        if (!field) rejected|=audit::no_field;
        if (eta<0 || eta>14) rejected|=audit::eta_range;
        if (!std::isfinite(price) || price<0 || price>kPriceUpper.back()) rejected|=audit::price_range;
        audit::bid(s.request,now,1,std::to_integer<std::uint8_t>(bid),eta,price,ctx.pos.x,ctx.pos.y,field?1:0,rejected,checked,7);
    }
    (void)farm_group;
    };
    publish_bid();
    if constexpr (cfg::joint_census) {
        if (!s.snapshot_ready && now==150) { s.snapshot=s.counts; s.snapshot_ready=true; }
        if (s.snapshot_ready && !s.emitted && now>=151 && fuel_ok(45000)) {
            const auto& n=s.snapshot;
            MB_LOG("J r150 i%d rq%u bi%u co%u ab%u as%u ar%u ha%u mv%u at%u ca%u ex%u mi%u ne%u fa%u\n",
                kn.self_index,n.requests,n.bids,n.commits,n.aborts,n.assigned,n.arrivals,n.helper_arrivals,
                n.moves,n.attacks,n.casts,n.expired,n.missed,n.noeffect,n.farm);
            s.emitted=true;
        }
    }
}
}
