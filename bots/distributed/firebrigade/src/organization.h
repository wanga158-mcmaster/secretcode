#pragma once
#include "profile.h"
#include "../../common/src/organization.h"
#include <cstdlib>

namespace p6::firebrigade {
struct ExecutablePolicy {
    enum class Incident { hold, approach, returning };
    struct State {
        int group = -1, owner = -1, epoch = -1, turns = 0, last_round = -1;
        int retired_group = -1, retired_epoch = -1;
        Gems previous_bag = 0, previous_score = 0;
        Channel previous_channel = Channel::none;
        bool previous_hold = false, previous_bank_eligible = false;
        Incident incident = Incident::hold;
    } state;
    static const organization::Profile& profile() {
        static const organization::Profile value{"firebrigade", roster};
        return value;
    }
    static long long distance(Point a, Point b) {
        return std::max(std::llabs(static_cast<long long>(a.x) - b.x),
                        std::llabs(static_cast<long long>(a.y) - b.y));
    }
    static bool hostile_reach(const organization::Input& in, const Actor& owner) {
        for (int i = 0; i < in.frame.actor_count; ++i) {
            const auto* enemy = in.frame.visible(in.frame.actors[i].subject);
            if (enemy && enemy->team == Team::enemy && enemy->alive &&
                enemy->strength > 0 && enemy->attack_range >= 0 &&
                distance(enemy->position, owner.position) <= enemy->attack_range) return true;
        }
        return false;
    }
    static bool alarm(const organization::Input& in, const organization::Site& site,
                      const Actor& owner, Incident& kind) {
        if (!site.known || !owner.alive || !roster_actor(owner, in.picks) ||
            owner.pick_ordinal > 1 || owner.id == in.frame.clock.self) return false;
        if (hostile_reach(in, owner)) {
            if (site.group.lifecycle == Lifecycle::reward_available &&
                owner.spawn_group == site.group.id &&
                owner.action == WitnessAction::occupied_spawn) {
                kind = Incident::hold; return true;
            }
            if (owner.bag > 0) { kind = Incident::returning; return true; }
        }
        if (owner.channel != Channel::none || distance(owner.position, site.group.approach) > 1 ||
            owner.position == site.group.approach) return false;
        for (int i = 0; i < in.frame.actor_count; ++i) {
            const auto* enemy = in.frame.visible(in.frame.actors[i].subject);
            if (enemy && enemy->team == Team::enemy && enemy->alive && enemy->strength > 0 &&
                enemy->position == site.group.approach) {
                kind = Incident::approach; return true;
            }
        }
        return false;
    }
    void retire() {
        state.retired_group = state.group; state.retired_epoch = state.epoch;
        state.group = state.owner = -1; state.turns = 0; state.last_round = -1;
        state.previous_hold = state.previous_bank_eligible = false;
        state.previous_channel = Channel::none;
    }
    void remember(const organization::Input& in, const organization::Site& site,
                  const Actor& owner) {
        state.previous_bag = owner.bag; state.previous_score = in.frame.own_team_score;
        state.previous_channel = owner.channel;
        state.previous_bank_eligible = owner.channel == Channel::tribute &&
            owner.channel_valid && owner.bag > 0;
        state.previous_hold = site.known && site.group.lifecycle == Lifecycle::reward_available &&
            site.group.uncontested_now && owner.spawn_group == site.group.id;
    }
    static organization::Decision local_work(const organization::Input& in, bool control) {
        if (control) return organization::independent(in);
        const organization::Site* nearest = nullptr;
        for (int i = 0; i < in.site_count; ++i) {
            const auto& site = in.sites[i];
            if (!site.known || site.blocked || !site.solo(in.frame, in.frame.clock.self)) continue;
            if (!nearest || distance(in.frame.own.actor.position, site.group.approach) <
                distance(in.frame.own.actor.position, nearest->group.approach) ||
                (distance(in.frame.own.actor.position, site.group.approach) ==
                 distance(in.frame.own.actor.position, nearest->group.approach) &&
                 site.group.id < nearest->group.id)) nearest = &site;
        }
        return nearest ? organization::work(nearest->group.id, "firebrigade-local-productive-reserve") :
            organization::independent(in);
    }
    organization::Decision choose(const organization::Input& in, bool control) {
        using namespace organization;
        const auto& self = in.frame.own.actor;
        if (!in.frame.valid() || !roster_actor(self, in.picks) || !responder(self))
            return independent(in);
        if (!in.safe_now || !self.alive || self.channel != Channel::none) {
            if (state.group >= 0) retire();
            return independent(in);
        }
        // Preserve the responder's real safe hold before an optional alarm.
        if (in.phase == Phase::hold) {
            const auto* capture = in.site(in.current_group);
            if (capture && capture->known && capture->group.lifecycle == Lifecycle::reward_available &&
                capture->group.uncontested_now && self.spawn_group == capture->group.id &&
                capture->solo(in.frame, self.id)) {
                if (state.group >= 0) retire();
                return work(capture->group.id, "firebrigade-preserve-actual-capture");
            }
        }
        if (state.group >= 0) {
            const auto* site = in.site(state.group);
            const auto* owner = in.frame.visible(state.owner);
            if (!site || !site->known || !owner || !owner->alive ||
                !roster_actor(*owner, in.picks)) {
                retire(); return local_work(in, control);
            }
            const bool safe_owner = !hostile_reach(in, *owner);
            const bool consecutive = in.frame.clock.round == state.last_round + 1;
            // Each completion is a physical observation of this owner, not the
            // responder's unrelated payout flag or a predicted timer expiry.
            const bool payout = consecutive && safe_owner && state.previous_hold &&
                site->group.lifecycle == Lifecycle::respawning && owner->bag > state.previous_bag;
            const bool bank = consecutive && safe_owner && state.previous_bank_eligible &&
                state.previous_channel == Channel::tribute &&
                owner->channel == Channel::none && state.previous_bag > 0 && owner->bag == 0 &&
                in.frame.own_team_score > state.previous_score;
            const bool recovery = safe_owner && owner->position == site->group.retreat &&
                owner->channel == Channel::none && owner->health >= .2 * owner->max_health;
            if (payout || bank || recovery) {
                retire(); auto d = local_work(in, control);
                d.reason = payout ? "firebrigade-witnessed-safe-payout" :
                    bank ? "firebrigade-witnessed-protected-bank" : "firebrigade-recovery-waypoint";
                return d;
            }
            if (state.last_round != in.frame.clock.round && state.turns >= intervention_limit) {
                retire(); auto d = local_work(in, control);
                d.reason = "firebrigade-eight-turn-limit"; return d;
            }
            const auto* certificate = site->cycle(self.id);
            if (!certificate || !certificate->valid(in.frame, self.id, site->group.id)) {
                retire(); return local_work(in, control);
            }
            if (state.last_round != in.frame.clock.round) {
                state.last_round = in.frame.clock.round; ++state.turns;
            }
            remember(in, *site, *owner);
            return {state.incident == Incident::returning ? Intent::escort : Intent::intercept,
                state.group, state.owner, owner->position, true, "firebrigade-bounded-intervention"};
        }
        if (control) return local_work(in, true);
        for (int s = 0; s < in.site_count; ++s) {
            const auto& site = in.sites[s];
            if (!site.known || site.blocked || !site.joint_better ||
                (state.retired_group == site.group.id &&
                 state.retired_epoch == in.frame.clock.round / 64)) continue;
            const auto* certificate = site.cycle(self.id);
            if (!certificate || !certificate->valid(in.frame, self.id, site.group.id)) continue;
            for (int pick = 0; pick < 2; ++pick) {
                const auto* owner = in.ordinal_actor(pick);
                Incident kind{};
                if (!owner || site.holder != owner->id || !alarm(in, site, *owner, kind)) continue;
                const auto* owner_cycle = site.cycle(owner->id);
                if (!owner_cycle || !owner_cycle->valid(in.frame, owner->id, site.group.id)) continue;
                state.group = site.group.id; state.owner = owner->id;
                state.epoch = in.frame.clock.round / 64; state.turns = 1;
                state.last_round = in.frame.clock.round; state.incident = kind;
                remember(in, site, *owner);
                return {kind == Incident::returning ? Intent::escort : Intent::intercept,
                    site.group.id, owner->id, owner->position, true, "firebrigade-witnessed-profitable-alarm"};
            }
        }
        return local_work(in, false);
    }
};
} // namespace p6::firebrigade
