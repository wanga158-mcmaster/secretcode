#pragma once
#include "profile.h"
#include "../../common/src/organization.h"

namespace p6::replicas {
struct ExecutablePolicy {
    struct State {
        int group = -1, epoch = -1, repeat_epoch = -1;
        int waiting = 0, last_wait_round = -1;
        bool started = false, closed = false, released = false;
        bool owner_seen = false, repeat_used = false, pad_reached = false;
    } state;
#if P6_REPLICAS_STARTUP_PACKET
    bool startup_packet_done = false, startup_packet_pending = false;
    int startup_close_cause = 0;
    std::array<std::int64_t, 40> startup_packet{};
#endif
    static const organization::Profile& profile() {
        static const organization::Profile value{"replicas", roster};
        return value;
    }
    static const organization::Site* named_source(const organization::Input& in) {
        const int pick = in.frame.own.actor.pick_ordinal;
        if (pick >= 1 && pick < in.site_count) {
            const auto& site = in.sites[pick];
            if (site.group.id >= 0 && site.group.id != in.proposed_group && !site.blocked) return &site;
        }
        const auto* fallback = in.site(in.independent_group);
        return fallback && fallback->group.id != in.proposed_group && !fallback->blocked ? fallback : nullptr;
    }
    static organization::Decision independent_continuation(const organization::Input& in) {
        using namespace organization;
        const auto& self = in.frame.own.actor;
        if (self.channel != Channel::none && self.channel_valid && in.safe_now)
            return {Intent::independent,in.current_group,-1,{},false,"replicas-preserve-actual-channel"};
        const auto* current = in.site(in.current_group);
        if (in.safe_now && in.phase == Phase::hold && current && current->known &&
            current->group.lifecycle == Lifecycle::reward_available && current->group.uncontested_now &&
            self.spawn_group == current->group.id && current->solo(in.frame,self.id))
            return work(current->group.id,"replicas-preserve-actual-capture");
#if P6_REPLICAS_INSPECTION_REPAIR
        // A personally known independent project already admitted by the
        // executor must reach capture/conversion through that same executor.
        // A missing replacement quote is not a reason to inspect another pad.
        // The startup owner/spare project keeps its separate release protocol.
        if (in.safe_now && current && current->known &&
            current->group.id != in.proposed_group && in.phase != Phase::bootstrap)
            return work(current->group.id,"replicas-preserve-owned-independent-cycle");
#endif
        if (self.pick_ordinal < 1) return independent(in);
        const auto* source = named_source(in);
        if (source && source->known && source->solo(in.frame,self.id))
            return {Intent::independent,source->group.id,-1,{},false,"replicas-static-independent-source"};
#if P6_REPLICAS_PARTIAL_INSPECTION_REPAIR
        // A visible member establishes lifecycle, not the complete source.
        // An idle empty actor finishes that same personal inspection and its
        // deferred approach through the ordinary work adapter. At the pad or a
        // current legal work location, the normal complete safe quote decides.
        // Neither a hidden member nor an incomplete quote is assumed usable.
        const bool approach_pending = source && self.position != source->group.approach &&
            self.ready_attack_group != source->group.id && self.spawn_group != source->group.id;
        if (source && source->known && (!source->group.all_spawns_visible || approach_pending) &&
            source->group.lifecycle == Lifecycle::living && in.safe_now &&
            self.bag == 0 && self.channel == Channel::none &&
            (in.current_group < 0 || in.current_group == source->group.id))
            return work(source->group.id,"replicas-continue-partial-source-inspection");
#endif
#if P6_REPLICAS_INSPECTION_REPAIR
        // Arrival is not source knowledge. Ask the existing work adapter to
        // request this group's next personal observation; its ordinary quote,
        // visibility, hazard and executor guards still authorize every action.
        if (source && !source->known && in.safe_now && self.position == source->group.approach)
            return work(source->group.id,"replicas-arrived-inspection-request");
#endif
        if (source && !source->known && in.safe_now)
            return waypoint(Intent::approach,source->group.id,source->group.approach,"replicas-personal-fallback-inspection");
        // An unavailable static assignment gives no ownership over startup or a
        // different worker's source. A distinct current safe fallback is usable.
        const auto* fallback = in.site(in.independent_group);
        if (fallback && fallback->group.id != in.proposed_group && !fallback->blocked &&
            fallback->known && fallback->solo(in.frame,self.id))
            return {Intent::independent,fallback->group.id,-1,{},false,"replicas-available-safe-fallback"};
        for (int i = 0; i < in.site_count; ++i) {
            const auto& site = in.sites[i];
            if (site.group.id != in.proposed_group && !site.blocked && site.known && site.solo(in.frame,self.id))
                return {Intent::independent,site.group.id,-1,{},false,"replicas-available-safe-fallback"};
        }
        return {self.bag > 0 ? Intent::bank : Intent::recover,-1,-1,{},false,"replicas-unavailable-independent-source"};
    }
    static bool production_witness(const organization::Input& in,
                                    const organization::Site& site, const Actor* owner) {
        if (!organization::ready(owner) || owner->pick_ordinal != 0 ||
            !roster_actor(*owner, in.picks) || !site.known ||
            !site.solo(in.frame, owner->id)) return false;
        return (site.group.lifecycle == Lifecycle::living &&
                owner->action == WitnessAction::useful_attack &&
                owner->action_group == site.group.id) ||
            (site.group.lifecycle == Lifecycle::reward_available &&
             site.group.uncontested_now && owner->spawn_group == site.group.id);
    }
#if P6_REPLICAS_STARTUP_DISPATCH_REPAIR
    // Independent candidate diversity is not an exclusion of this named
    // owner/spare program. Only the just-produced explicit work request may
    // use this exception; approach, release, retirement and fallback cannot.
    bool explicit_startup_work(const organization::Input& in,
                               const organization::Decision& decision) const {
        const auto& self = in.frame.own.actor;
        const auto* site = in.site(state.group);
        return decision.intent == organization::Intent::work &&
            decision.group == state.group && state.group == in.proposed_group &&
            state.started && !state.closed && !state.released &&
            in.frame.valid() && in.safe_now && self.pick_ordinal == 1 &&
            roster_actor(self, in.picks) && self.alive && self.health > 0 &&
            self.bag == 0 && self.channel == Channel::none &&
            site && !site->blocked && site->known && site->group.all_spawns_visible &&
            site->solo(in.frame, self.id);
    }
#endif
#if P6_REPLICAS_STARTUP_PACKET
    organization::Decision choose_impl(const organization::Input& in, bool control) {
#else
    organization::Decision choose(const organization::Input& in, bool control) {
#endif
        using namespace organization;
        const auto& self = in.frame.own.actor;
#if P6_REPLICAS_STARTUP_ALIAS_REPAIR
        // The catalogue's preferred source can occupy the spare's ordinal slot.
        // That does not erase its existing distinct named fallback. Keep the
        // startup contract open; its ordinary personal witnesses and executor
        // still decide release, takeover and every production action.
        const bool alias_with_fallback = self.pick_ordinal == 1 && in.site_count > 1 &&
            !in.sites[1].blocked && in.sites[1].group.id == in.proposed_group && named_source(in);
        if (self.pick_ordinal >= 1 &&
            (self.pick_ordinal >= in.site_count || in.sites[self.pick_ordinal].blocked ||
             in.sites[self.pick_ordinal].group.id == in.proposed_group) && !alias_with_fallback) {
#if P6_REPLICAS_STARTUP_PACKET
            startup_close_cause = self.pick_ordinal >= in.site_count ? 1 :
                in.sites[self.pick_ordinal].blocked ? 2 : 3;
#endif
            state.closed = true;
        }
#else
#if P6_REPLICAS_STARTUP_PACKET
        if (self.pick_ordinal >= 1 &&
            (self.pick_ordinal >= in.site_count || in.sites[self.pick_ordinal].blocked ||
             in.sites[self.pick_ordinal].group.id == in.proposed_group)) {
            startup_close_cause = self.pick_ordinal >= in.site_count ? 1 :
                in.sites[self.pick_ordinal].blocked ? 2 : 3;
            state.closed = true;
        }
#else
        if (self.pick_ordinal >= 1 &&
            (self.pick_ordinal >= in.site_count || in.sites[self.pick_ordinal].blocked ||
             in.sites[self.pick_ordinal].group.id == in.proposed_group)) state.closed = true;
#endif
#endif
        if (!in.frame.valid() || !roster_actor(self, in.picks) || !in.safe_now ||
            self.pick_ordinal > 1 || (control && self.pick_ordinal == 1)) return independent_continuation(in);
        if ((self.channel != Channel::none && self.channel_valid) ||
            (in.phase == Phase::hold && self.spawn_group == in.current_group)) {
            const auto preserve = independent_continuation(in);
            if (self.channel != Channel::none || preserve.group == in.current_group) return preserve;
        }
        const int epoch = in.frame.clock.round / 64;
        if (state.group < 0) state.group = in.proposed_group;
        if (state.group < 0) return independent_continuation(in);
        const auto* site = in.site(state.group);
        const auto* owner = in.ordinal_actor(0);
        const bool owner_available = ready(owner) && roster_actor(*owner, in.picks);
        // Only an observed failure grants a single attempt in the *next* epoch.
        // Merely losing personal contact never grants retry credit.
        const bool failed = in.observed_failure || in.hidden_collision ||
            (owner && (!owner->alive || owner->channel == Channel::recall ||
                       owner->channel == Channel::teleport));
        if (state.started && !state.closed && failed) {
            if (!control && !state.repeat_used && state.repeat_epoch < 0)
                state.repeat_epoch = state.epoch + 1;
            state.closed = true;
        }
        if (!control && state.closed && !state.repeat_used && state.repeat_epoch == epoch) {
            state.closed = false; state.released = false; state.repeat_used = true;
            state.started = false; state.pad_reached = false;
            state.owner_seen = false; state.waiting = 0;
        }
        if (state.closed) return independent_continuation(in);
        if (in.current_group == state.group && in.actual_payout) {
            state.closed = true;
            return {Intent::bank, state.group, -1, {}, false, "replicas-actual-payout-bank"};
        }
        if (!site || site->blocked || site->group.approach == site->group.retreat) {
            state.closed = true; return independent_continuation(in);
        }
        if (!state.started) { state.started = true; state.epoch = epoch; }
        const auto continuation = independent_continuation(in);
        const auto* fallback = in.site(continuation.group);
        if (self.pick_ordinal == 1 && production_witness(in, *site, owner) &&
            fallback && fallback->solo(in.frame, self.id)) {
            state.closed = state.released = true;
            auto d = continuation; d.reason = "replicas-personally-witnessed-release";
            return d;
        }
        if (self.pick_ordinal == 1 && !owner_available &&
            (state.owner_seen || site->known)) {
            if (site->known && site->solo(in.frame, self.id))
                return work(state.group, "replicas-current-view-takeover");
            state.closed = true; return independent_continuation(in);
        }
        state.owner_seen |= owner_available;
        const Point pad = self.pick_ordinal == 0 ? site->group.approach : site->group.retreat;
        state.pad_reached |= self.position == pad;
        if (!state.pad_reached)
            return waypoint(Intent::approach, state.group, pad, "replicas-distinct-startup-pad");
        if (site->known && site->solo(in.frame, self.id))
            return work(state.group, self.pick_ordinal == 0 ?
                "replicas-startup-owner" : "replicas-redundant-production");
        if (state.last_wait_round != in.frame.clock.round) {
            state.last_wait_round = in.frame.clock.round; ++state.waiting;
        }
        if (state.waiting >= 8) { state.closed = true; return independent_continuation(in); }
        // Uncertain source: personally inspect the assigned pad. This never
        // licenses attacking unseen or unsafe camps and is not a release proof.
        return waypoint(Intent::approach, state.group, pad, "replicas-inspect-uncertain-source");
    }
#if P6_REPLICAS_STARTUP_PACKET
    organization::Decision choose(const organization::Input& in, bool control) {
        const auto& self = in.frame.own.actor;
        if (startup_packet_done || self.pick_ordinal < 0 || self.pick_ordinal > 1)
            return choose_impl(in, control);
        startup_packet_done = true;
        const auto before = state;
        const auto decision = choose_impl(in, control);
        const auto* owner = in.ordinal_actor(0);
        const auto* site = in.site(in.proposed_group);
        const auto* fallback = named_source(in);
        std::uint32_t hash = 0;
        if (decision.reason) {
            hash = 2166136261u;
            for (const auto* c = decision.reason; *c; ++c)
                hash = (hash ^ static_cast<unsigned char>(*c)) * 16777619u;
        }
        startup_packet = {in.frame.clock.round, self.id, control, self.pick_ordinal, in.site_count,
            in.site_count > 0 ? in.sites[0].group.id : -1,
            in.site_count > 1 ? in.sites[1].group.id : -1,
            in.site_count > 1 ? in.sites[1].blocked : 0, in.proposed_group, in.independent_group,
            in.current_group, in.safe_now, in.frame.valid(), before.closed, state.closed,
            before.started, state.started, before.group, state.group, before.released, state.released,
            before.waiting, state.waiting, owner ? owner->id : -1, organization::ready(owner),
            owner ? static_cast<int>(owner->action) : -1, owner ? owner->action_group : -1,
            owner ? owner->spawn_group : -1, site ? site->known : 0,
            site ? site->group.all_spawns_visible : 0, site && owner ? site->solo(in.frame, owner->id) : 0,
            startup_close_cause, static_cast<int>(decision.intent), decision.group, hash,
            fallback ? fallback->group.id : -1, self.position.x, self.position.y, self.bag, 0};
        startup_packet_pending = true;
        return decision;
    }
#endif
};
} // namespace p6::replicas
