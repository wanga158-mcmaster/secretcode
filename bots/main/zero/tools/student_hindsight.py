"""Pure next-interaction target forecasts from already canonical source events.

This module decodes no engine kinds, observations, features or private expert intent.
``source_intent``/the source-record producer must first identify each observed action,
its paired origins, alive episode and optional unique GoalSpec. Events are ordered by
dispatch origin, not by when a delayed receipt becomes available. The current dispatch
is included: the forecast asks for the next interaction from the pre-dispatch prefix.

A forecast is a DERIVED label. Native ``student_goal`` must check its membership at
that prefix; future positions, visibility, HP and candidates are never supplied here.
Only a separately declared downstream conditioning profile may apply a changed goal
through student_history.h, with available REPLAN and at most one per owner turn.
This module changes no history, emits no CE and changes no source eligibility.

Origin fields match student_pack.ORIGIN_FIELDS without importing its numpy dependency.
GoalSpec uses the existing C++ GoalKind IDs. ``non_interaction`` must be positively
identified; unknown or unresolved events are barriers, not events to look past.
Run --self-test for bounded synthetic contract tests only.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Mapping


PROPOSAL_VERSION = 'next-observed-interaction/v1'
LABEL_KIND = 'derived_next_observed_interaction'
ORIGIN_FIELDS = ('sequence', 'input', 'actor', 'team', 'round', 'phase', 'native_kind')
INTERACTION_GOALS = {'attack_camp': 0, 'attack_enemy': 3, 'tribute': 1,
                     'explicit_recall': 2, 'ally_heal': 6}
RESOLUTIONS = ('unique', 'ambiguous', 'unsupported', 'unresolved')


def _int(value, name, minimum=-(1 << 31)):
    if type(value) is not int or not minimum <= value < (1 << 31):
        raise ValueError(f'{name}: expected int32 >= {minimum}')
    return value


def _text(value, name):
    if not isinstance(value, str) or not value:
        raise ValueError(f'{name}: expected nonempty string')
    return value


def _maybe_bool(value, name):
    if value is not None and type(value) is not bool:
        raise ValueError(f'{name}: expected bool or None')


@dataclass(frozen=True)
class Origin:
    sequence: int
    input: int
    actor: int
    team: int
    round: int
    phase: int
    native_kind: int

    def __post_init__(self):
        for field in ORIGIN_FIELDS:
            _int(getattr(self, field), field)
        if self.sequence < 0 or self.input < 1 or self.actor < 0 or self.team not in (0, 1) or self.round < 0:
            raise ValueError('invalid gameplay origin')

    @classmethod
    def from_mapping(cls, value: Mapping):
        return cls(**{field: value[field] for field in ORIGIN_FIELDS})

    @property
    def order(self):
        # Canonical source sequence/input establish total order; rounds do not.
        return self.sequence, self.input


@dataclass(frozen=True)
class GoalSpec:
    kind: int
    target: int

    def __post_init__(self):
        _int(self.kind, 'goal.kind', 0)
        _int(self.target, 'goal.target')
        if self.kind not in INTERACTION_GOALS.values():
            raise ValueError('only canonical interaction goals are forecastable')
        if self.kind == 2:
            if self.target != -1:
                raise ValueError('Recall target must be -1')
        elif self.target < 0:
            raise ValueError('interaction target must be a canonical nonnegative ID')


@dataclass(frozen=True)
class DecisionPoint:
    origin: Origin
    owner_turn: str
    alive_episode: int | None
    alive: bool | None

    def __post_init__(self):
        _text(self.owner_turn, 'owner_turn')
        _maybe_bool(self.alive, 'alive')
        if self.alive_episode is not None:
            _int(self.alive_episode, 'alive_episode', 0)


@dataclass(frozen=True)
class CanonicalEvent:
    event_id: str
    decision_origin: Origin
    label_origin: Origin | None
    owner_turn: str
    alive_episode: int | None
    alive_before: bool | None
    alive_after: bool | None
    interaction: str
    resolution: str
    goal_spec: GoalSpec | None = None
    event_role: str = 'observed_engine_action'
    proof_ref: str | None = None
    interaction_before_death: bool | None = None

    def __post_init__(self):
        _text(self.event_id, 'event_id')
        _text(self.owner_turn, 'owner_turn')
        if self.event_role != 'observed_engine_action':
            raise ValueError('only paired canonical observed actions are accepted; receipts are not new actions')
        if self.interaction not in (*INTERACTION_GOALS, 'non_interaction', 'unknown'):
            raise ValueError('unknown canonical interaction name')
        if self.resolution not in RESOLUTIONS:
            raise ValueError('unknown canonical resolution')
        for name in ('alive_before', 'alive_after', 'interaction_before_death'):
            _maybe_bool(getattr(self, name), name)
        if self.alive_episode is not None:
            _int(self.alive_episode, 'alive_episode', 0)
        if self.label_origin is not None and self.label_origin.order < self.decision_origin.order:
            raise ValueError('label evidence precedes its dispatch')
        if self.goal_spec is not None:
            if self.resolution != 'unique' or INTERACTION_GOALS.get(self.interaction) != self.goal_spec.kind:
                raise ValueError('GoalSpec must match a unique canonical interaction')
            if self.interaction == 'ally_heal' and self.goal_spec.target == self.decision_origin.actor:
                raise ValueError('self heal is not a targeted allied recipient')
        if self.resolution == 'unique' and self.interaction in INTERACTION_GOALS and self.goal_spec is None:
            raise ValueError('unique interaction requires its native-resolved GoalSpec')
        if self.resolution == 'unique' and self.interaction != 'unknown' and self.label_origin is None:
            raise ValueError('resolved canonical event requires evidence origin')


@dataclass(frozen=True)
class Window:
    source_ref: str
    source_version: str
    start_origin: Origin
    end_origin: Origin
    events: tuple[CanonicalEvent, ...]
    left_censored: bool = False
    right_censored: bool = True

    def __post_init__(self):
        _text(self.source_ref, 'source_ref')  # An immutable source ID/hash, supplied by the producer.
        _text(self.source_version, 'source_version')
        if self.start_origin.order > self.end_origin.order:
            raise ValueError('reversed source window')
        if type(self.left_censored) is not bool or type(self.right_censored) is not bool:
            raise ValueError('window censor flags must be bool')
        previous = None
        ids = set()
        for event in self.events:
            order = event.decision_origin.order
            if previous is not None and order <= previous:
                raise ValueError('canonical dispatch origins must be strictly ordered; normalize aliases/conflicts upstream')
            if not self.start_origin.order <= order <= self.end_origin.order:
                raise ValueError('canonical dispatch outside source window')
            if event.event_id in ids:
                raise ValueError('duplicate canonical event ID')
            previous = order
            ids.add(event.event_id)


@dataclass(frozen=True)
class Proposal:
    source_ref: str
    source_version: str
    decision_origin: Origin
    owner_turn: str
    alive_episode: int | None
    status: str
    reason: str
    interaction_origin: Origin | None = None
    label_origin: Origin | None = None
    event_id: str | None = None
    interaction: str | None = None
    goal_spec: GoalSpec | None = None
    barrier_origin: Origin | None = None
    left_censored: bool = False
    right_censored: bool = True
    proposal_version: str = PROPOSAL_VERSION
    label_kind: str = LABEL_KIND
    membership: str = 'pending_shared_native_at_decision_prefix'
    source_eligibility_changed: bool = False

    def to_mapping(self):
        return asdict(self)


def propose(window: Window, decision: DecisionPoint) -> Proposal:
    """Return one forecast or explicit blocked/censored state; never invent a fallback goal."""
    def result(status, reason, event=None, barrier=None):
        return Proposal(window.source_ref, window.source_version, decision.origin,
                        decision.owner_turn, decision.alive_episode, status, reason,
                        event.decision_origin if event else None,
                        event.label_origin if event else None,
                        event.event_id if event else None,
                        event.interaction if event else None,
                        event.goal_spec if event and status == 'proposed' else None,
                        barrier, window.left_censored, window.right_censored)

    if not window.start_origin.order <= decision.origin.order <= window.end_origin.order:
        return result('outside_window', 'decision dispatch is outside the declared source window')
    if decision.alive is False:
        return result('not_alive', 'decision is observed dead')
    if decision.alive is None or decision.alive_episode is None:
        return result('ambiguous', 'decision alive episode is unavailable')
    start = next((i for i, e in enumerate(window.events) if e.decision_origin == decision.origin), None)
    if start is None:
        return result('outside_window', 'no canonical action at the exact decision origin')
    first = window.events[start]
    if first.owner_turn != decision.owner_turn:
        raise ValueError('decision owner turn differs from canonical source action')
    for event in window.events[start:]:
        if (event.decision_origin.actor, event.decision_origin.team) != (decision.origin.actor, decision.origin.team):
            continue
        if event.alive_before is False or (event.alive_episode is not None and event.alive_episode != decision.alive_episode):
            return result('cross_death', 'next interaction would cross the observed alive episode', event, event.decision_origin)
        if event.alive_before is None or event.alive_episode is None:
            return result('ambiguous', 'observed alive-episode continuity is unavailable', event, event.decision_origin)
        if event.resolution != 'unique':
            status = 'window_censored' if event.resolution == 'unresolved' else event.resolution
            return result(status, 'earlier canonical action is not uniquely resolved', event, event.decision_origin)
        if event.interaction == 'unknown':
            return result('unsupported', 'earlier action has unknown interaction semantics', event, event.decision_origin)
        if event.label_origin is not None and event.label_origin.order > window.end_origin.order:
            return result('window_censored', 'interaction evidence is outside the source window', event, event.decision_origin)
        if event.interaction != 'non_interaction':
            if event.interaction == 'ally_heal' and not event.proof_ref:
                return result('unsupported', 'Assist requires native proof of the unique targeted ally heal', event, event.decision_origin)
            if event.alive_after is False and event.interaction_before_death is not True:
                status = 'cross_death' if event.interaction_before_death is False else 'ambiguous'
                return result(status, 'interaction/death order is not proved within the alive episode', event, event.decision_origin)
            # A delayed resolution must not silently step across a separately observed death.
            for boundary in window.events[start:]:
                if boundary.decision_origin.order > event.label_origin.order:
                    break
                if boundary.decision_origin.order <= event.decision_origin.order:
                    continue
                if (boundary.decision_origin.actor, boundary.decision_origin.team) == (decision.origin.actor, decision.origin.team):
                    if boundary.alive_before is None or boundary.alive_after is None or boundary.alive_episode is None:
                        return result('ambiguous', 'delayed evidence has unknown alive-episode continuity', event, boundary.decision_origin)
                    if boundary.alive_before is False or boundary.alive_after is False or boundary.alive_episode != decision.alive_episode:
                        return result('cross_death', 'delayed evidence crosses an observed death boundary', event, boundary.decision_origin)
            return result('proposed', 'next uniquely identified observed interaction; not expert strategic intent', event)
        if event.alive_after is False:
            return result('cross_death', 'observed episode ended before any interaction', event, event.decision_origin)
        if event.alive_after is None:
            return result('ambiguous', 'noninteraction outcome does not establish alive continuity', event, event.decision_origin)
    status = 'window_censored' if window.right_censored else 'no_future_interaction'
    return result(status, 'no identified interaction before the declared source window ended')


@dataclass(frozen=True)
class NativeVerdict:
    """Downstream facts only: obtain membership from shared student_goal at decision prefix."""
    source_ref: str
    source_version: str
    decision_origin: Origin
    owner_turn: str
    goal_spec: GoalSpec
    proof_ref: str
    membership: str  # member, not_member, unsupported
    replan_available: bool
    goal_changed: bool
    already_replanned_this_owner_turn: bool

    def __post_init__(self):
        _text(self.source_ref, 'source_ref')
        _text(self.source_version, 'source_version')
        _text(self.owner_turn, 'owner_turn')
        _text(self.proof_ref, 'native proof_ref')
        if self.membership not in ('member', 'not_member', 'unsupported'):
            raise ValueError('native membership must be explicit')
        for field in ('replan_available', 'goal_changed', 'already_replanned_this_owner_turn'):
            if type(getattr(self, field)) is not bool:
                raise ValueError(f'{field}: expected bool')


def expected_stage_audit(proposal: Proposal, verdict: NativeVerdict | None = None) -> dict:
    """Audit potential conditioning stages; neither apply a transition nor authorize CE."""
    status, stages = 'no_proposal', ()
    if proposal.status == 'proposed':
        stages = ('root', 'goal')
        status = 'pending_shared_native'
        if verdict is not None:
            if (verdict.source_ref != proposal.source_ref or verdict.source_version != proposal.source_version
                    or verdict.decision_origin != proposal.decision_origin or verdict.owner_turn != proposal.owner_turn
                    or verdict.goal_spec != proposal.goal_spec):
                raise ValueError('native verdict is rebound to a different decision/goal')
            if verdict.membership != 'member':
                status = 'blocked_' + verdict.membership
            elif not verdict.goal_changed:
                status, stages = 'unchanged_no_transition', ()
            elif not verdict.replan_available:
                status = 'blocked_replan_unavailable'
            elif verdict.already_replanned_this_owner_turn:
                status = 'blocked_owner_turn_already_replanned'
            else:
                status = 'ready_for_declared_conditioning_profile'
    elif verdict is not None:
        raise ValueError('cannot apply a native verdict without a proposed target')
    return {'proposal': proposal.to_mapping(), 'conditioning_status': status,
            'expected_stages': list(stages), 'stage_semantics': 'derived_control_conditioning_only',
            'native_verdict': asdict(verdict) if verdict else None,
            'history_transition_applied': False, 'ce_emitted': False, 'optimizer_allowed': False,
            'observed_primitive_ce_changed': False, 'source_eligibility_changed': False}


def _self_test():
    """Synthetic contract cases; no source replay, feature construction or dataset emission."""
    import unittest
    from dataclasses import replace

    def origin(i, actor=0, round_=0):
        return Origin(i - 1, i, actor, 0, round_, 0, 0)

    def event(i, interaction='non_interaction', goal=None, **kw):
        return CanonicalEvent(str(i), origin(i), origin(i), 'turn0', 0, True, True,
                              interaction, 'unique', goal, **kw)

    def window(*events, **kw):
        return Window('sha256:synthetic-only', 'canonical-test/v1', origin(1), origin(100, round_=1000),
                      tuple(events), **kw)

    point = DecisionPoint(origin(1), 'turn0', 0, True)

    class Contract(unittest.TestCase):
        def test_current_and_first_future(self):
            now = event(1, 'attack_camp', GoalSpec(0, 500))
            self.assertEqual(propose(window(now), point).goal_spec, GoalSpec(0, 500))
            p = propose(window(event(1), event(2, 'attack_enemy', GoalSpec(3, 27)),
                               event(3, 'attack_camp', GoalSpec(0, 0))), point)
            self.assertEqual((p.interaction_origin.input, p.goal_spec), (2, GoalSpec(3, 27)))
            self.assertEqual(p.decision_origin, origin(1))

        def test_no_round_horizon(self):
            later = replace(event(2, 'tribute', GoalSpec(1, 0)), decision_origin=origin(2, round_=900),
                            label_origin=origin(2, round_=900))
            self.assertEqual(propose(window(event(1), later), point).status, 'proposed')

        def test_barriers(self):
            for resolution, expected in (('ambiguous', 'ambiguous'), ('unsupported', 'unsupported'),
                                         ('unresolved', 'window_censored')):
                barrier = replace(event(2), resolution=resolution)
                p = propose(window(event(1), barrier, event(3, 'attack_enemy', GoalSpec(3, 5))), point)
                self.assertEqual(p.status, expected)
                self.assertIsNone(p.goal_spec)
            self.assertEqual(propose(window(replace(event(1), interaction='unknown')), point).status, 'unsupported')

        def test_window_bounds_and_no_fallback(self):
            for right, expected in ((True, 'window_censored'), (False, 'no_future_interaction')):
                p = propose(window(event(1), right_censored=right, left_censored=True), point)
                self.assertEqual(p.status, expected)
                self.assertIsNone(p.goal_spec)
                self.assertTrue(p.left_censored)
            late = replace(event(1, 'explicit_recall', GoalSpec(2, -1)), label_origin=origin(101))
            self.assertEqual(propose(window(late), point).status, 'window_censored')
            self.assertEqual(propose(window(), point).status, 'outside_window')

        def test_alive_episode(self):
            attack = event(3, 'attack_enemy', GoalSpec(3, 1))
            dead = replace(event(2), alive_after=False)
            self.assertEqual(propose(window(event(1), dead, attack), point).status, 'cross_death')
            respawn = replace(attack, alive_episode=1)
            self.assertEqual(propose(window(event(1), respawn), point).status, 'cross_death')
            unknown = replace(event(2), alive_episode=None)
            self.assertEqual(propose(window(event(1), unknown, attack), point).status, 'ambiguous')
            self.assertEqual(propose(window(event(1)), replace(point, alive=False)).status, 'not_alive')

        def test_terminal_and_delayed_evidence(self):
            attack = replace(event(1, 'attack_enemy', GoalSpec(3, 1)), alive_after=False)
            self.assertEqual(propose(window(attack), point).status, 'ambiguous')
            self.assertEqual(propose(window(replace(attack, interaction_before_death=True)), point).status, 'proposed')
            delayed = replace(event(1, 'attack_enemy', GoalSpec(3, 1)), label_origin=origin(3))
            self.assertEqual(propose(window(delayed, replace(event(2), alive_after=False), event(3)), point).status, 'cross_death')
            self.assertEqual(propose(window(delayed, event(2), event(3)), point).label_origin, origin(3))
            self.assertEqual(propose(window(delayed, replace(event(2), alive_after=None), event(3)), point).status, 'ambiguous')

        def test_other_actor_and_assist_proof(self):
            other = replace(event(2, 'attack_enemy', GoalSpec(3, 1)), decision_origin=origin(2, actor=1),
                            label_origin=origin(2, actor=1))
            heal = event(3, 'ally_heal', GoalSpec(6, 2))
            self.assertEqual(propose(window(event(1), other, heal), point).status, 'unsupported')
            proved = replace(heal, proof_ref='native-targeted-ally-heal:case1')
            self.assertEqual(propose(window(event(1), other, proved), point).goal_spec, GoalSpec(6, 2))
            with self.assertRaises(ValueError):
                replace(proved, goal_spec=GoalSpec(6, 0))

        def test_malformed_or_duplicate_canonical_events(self):
            with self.assertRaises(ValueError):
                window(event(1), event(1))
            with self.assertRaises(ValueError):
                replace(event(1, 'attack_camp', GoalSpec(0, 0)), interaction='attack_enemy')
            with self.assertRaises(ValueError):
                GoalSpec(5, -1)  # no fabricated Hold/Avoid labels
            with self.assertRaises(ValueError):
                replace(event(1), event_role='observed_engine_resolution')

        def test_native_stage_gate_and_purity(self):
            events = (event(1, 'attack_enemy', GoalSpec(3, 9)),)
            p = propose(window(*events), point)
            self.assertEqual(expected_stage_audit(p)['conditioning_status'], 'pending_shared_native')
            verdict = NativeVerdict(p.source_ref, p.source_version, origin(1), 'turn0', GoalSpec(3, 9),
                                    'native-student-goal:synthetic1', 'not_member', True, True, False)
            audit = expected_stage_audit(p, verdict)
            self.assertEqual(audit['conditioning_status'], 'blocked_not_member')
            self.assertEqual(audit['expected_stages'], ['root', 'goal'])
            member = replace(verdict, membership='member')
            self.assertEqual(expected_stage_audit(p, replace(member, goal_changed=False))['expected_stages'], [])
            self.assertEqual(expected_stage_audit(p, replace(member, replan_available=False))['conditioning_status'], 'blocked_replan_unavailable')
            self.assertEqual(expected_stage_audit(p, replace(member, already_replanned_this_owner_turn=True))['conditioning_status'], 'blocked_owner_turn_already_replanned')
            self.assertEqual(expected_stage_audit(p, member)['conditioning_status'], 'ready_for_declared_conditioning_profile')
            with self.assertRaises(ValueError):
                expected_stage_audit(p, replace(member, decision_origin=origin(2)))
            with self.assertRaises(ValueError):
                expected_stage_audit(p, replace(member, owner_turn='other-turn'))
            with self.assertRaises(ValueError):
                expected_stage_audit(p, replace(member, source_ref='sha256:other-source'))
            with self.assertRaises(ValueError):
                expected_stage_audit(p, replace(member, source_version='canonical-other/v2'))
            self.assertFalse(audit['ce_emitted'])
            self.assertEqual(events[0], event(1, 'attack_enemy', GoalSpec(3, 9)))

    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromTestCase(Contract))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    import sys
    if sys.argv[1:] != ['--self-test']:
        raise SystemExit('pure import API; only --self-test is supported (no source/CE/pack runner)')
    raise SystemExit(_self_test())
