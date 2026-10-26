#pragma once
#include "profile.h"
#include "../../common/src/organization.h"

namespace p6::owner_builder {
// This executable boundary uses the shared, freshly built cycle comparisons.
// It does not manufacture schedules to satisfy the offline Option interface.
struct ExecutablePolicy {
    struct State { int helper_group = -1, owner = -1; } state;
    static const organization::Profile& profile() {
        static const organization::Profile value{"owner-builder", roster};
        return value;
    }
    static bool safe_cycle(const organization::Input& in,
                           const organization::Site& site, int actor) {
        const auto* c = site.cycle(actor);
        return c && c->valid(in.frame, actor, site.group.id);
    }
    static bool safe_hold(const organization::Input& in,
                          const organization::Site& site, const Actor* owner) {
        return organization::ready(owner) && site.known &&
            site.group.lifecycle == Lifecycle::reward_available &&
            site.group.uncontested_now && owner->spawn_group == site.group.id &&
            site.solo(in.frame, owner->id);
    }
    static organization::Decision control_alternate(const organization::Input& in) {
        const auto& self = in.frame.own.actor;
        const organization::Site* alternate = nullptr;
        for (int i = 0; i < in.site_count; ++i) {
            const auto& site = in.sites[i];
            if (!site.known || site.blocked || fixed_owner(self,site.group.id,in.picks) ||
                !site.solo(in.frame,self.id)) continue;
            bool occupied_work = false;
            for (int a = 0; a < in.frame.actor_count; ++a) {
                const auto* ally = in.frame.visible(in.frame.actors[a].subject);
                if (ally && ally->id != self.id && organization::useful(ally,site.group.id)) occupied_work = true;
            }
            if (occupied_work) continue;
            if (!alternate || site.group.id == in.independent_group ||
                (alternate->group.id != in.independent_group && site.group.id < alternate->group.id)) alternate = &site;
        }
        if (alternate) return organization::work(alternate->group.id,"owner-builder-control-alternate-source");
        return {organization::Intent::recover,-1,-1,{},false,"owner-builder-control-no-safe-alternate"};
    }
    organization::Decision choose(const organization::Input& in, bool control) {
        using namespace organization;
        const auto& self = in.frame.own.actor;
        if (!in.frame.valid() || !roster_actor(self, in.picks) || !in.safe_now)
            return independent(in);
        // An admitted helper remains through clearance, then releases only from
        // its own current view of a safe remaining holder. A lost participant
        // supplies no remote ownership or future contribution.
        if (state.helper_group >= 0) {
            const auto* site = in.site(state.helper_group);
            const auto* owner = in.frame.visible(state.owner);
            if (site && safe_hold(in, *site, owner)) {
                state = {};
                auto d = independent(in); d.reason = "owner-builder-safe-helper-release";
                return d;
            }
            if (site && site->known && ready(owner) &&
                site->solo(in.frame, owner->id) && safe_cycle(in, *site, self.id))
                return work(site->group.id, "owner-builder-admitted-helper");
            state = {};
            return independent(in);
        }
        // Fixed ownership is public geometry. Active owners never require a
        // helper's future turn. Control deliberately requires the current crew
        // and matching team gain even when its owner could survive alone. This
        // couples its actual admission, without inventing a stronger safety
        // certificate; the shared executor keeps its independent safety bound.
        const Site* own = nullptr;
        for (int i = 0; i < in.site_count; ++i) {
            const auto& site = in.sites[i];
            if (!site.known || site.blocked ||
                !fixed_owner(self, site.group.id, in.picks)) continue;
            bool admissible = !control && site.solo(in.frame, self.id);
            if (control && site.joint_better && site.holder == self.id &&
                safe_cycle(in, site, self.id)) {
                for (int a = 0; a < site.actor_count; ++a) {
                    const auto* helper = in.frame.visible(site.actors[a]);
                    if (helper && helper->id != self.id && roster_actor(*helper, in.picks) &&
                        useful(helper, site.group.id) && safe_cycle(in, site, helper->id))
                        admissible = true;
                }
            }
            if (!admissible) continue;
            if (!own || site.group.id == in.current_group ||
                (own->group.id != in.current_group && site.group.id < own->group.id)) own = &site;
        }
        if (own) return work(own->group.id, control ?
            "owner-builder-control-current-crew" : "owner-builder-self-sufficient-owner");
        if (control && self.pick_ordinal < core_picks) return control_alternate(in);
        for (int i = 0; i < in.site_count; ++i) {
            const auto& site = in.sites[i];
            const auto* owner = in.ordinal_actor(owner_pick(site.group.id));
            if (!site.known || site.blocked || !site.joint_better || !owner || site.holder != owner->id ||
                owner->id == self.id || !fixed_owner(*owner, site.group.id, in.picks) ||
                !useful(owner, site.group.id) || !site.solo(in.frame, owner->id) ||
                !safe_cycle(in, site, self.id) || safe_hold(in, site, owner)) continue;
            state.helper_group = site.group.id; state.owner = owner->id;
            return work(site.group.id, "owner-builder-useful-optional-helper");
        }
        return independent(in);
    }
};
} // namespace p6::owner_builder
